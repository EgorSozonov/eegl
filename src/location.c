//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## location.c: location lists (searches, errors from compilation, help greps) & marks (`ma`)

#include "eegl.h"
#include "h/data.types.h"
#include "h/data.h"
#include "h/book.h"
#include "h/input.types.h"
#include "h/input.h"
#include "h/channel.types.h"
#include "h/channel.h"
#include "h/diff.h"
#include "h/do.types.h"
#include "h/do.h"
#include "h/draw.types.h"
#include "h/draw.h"
#include "h/eval.h"
#include "h/fileio.h"
#include "h/hilite.types.h"
#include "h/hilite.h"
#include "h/message.h"
#include "h/option.h"
#include "h/portal.h"
#include "h/regexp.h"
#include "h/script.h"
#include "h/strings.h"
#include "h/tag.h"
#include "h/term.h"
#include "h/ui.h"
#include "h/wheel.types.h"
#include "h/wheel.h"

int fstat(int fd, struct stat* statbuf);
int stat(const char* restrict path, struct stat* restrict buf);
int lstat(const char* restrict, struct stat* restrict);

//{{{types

typedef struct ErrorFormatInfo ErrorFormatInfo;

typedef enum {
   SOURCE_FILENAME, //a proto-source, so to speak - will be turned into SOURCE_FILE after opening
   SOURCE_FILE, //reading locations from file
   SOURCE_BOOK, //reading locations from an Eegl buffer
   SOURCE_STRING, //reading locations from a big ole string
   SOURCE_LIST //reading location from a Var containing a list of strings
} SourceKind;

typedef struct { //SOURCE_FILENAME
   CS c;
} FileNameSource;

typedef struct { //SOURCE_FILE
   FILE* c;
} FileSource;

typedef struct { //SOURCE_BOOK
   Book* c;
   LineNr start;
   LineNr end;
} BookSource;

typedef struct { //SOURCE_STRING
   CS c;
} StringSource;

typedef struct { //SOURCE_LIST
   ListItem* c;
} ListSource;

typedef struct { //A source can be a file, a Book, a string var or a list vaar
   SourceKind tag;
   union {
      FileNameSource FileName;
      FileSource File;
      BookSource Book;
      StringSource String;
      ListSource List;
   };
} Source;

//State information used to parse lines and add entries to a quickfix/location list.
typedef struct {
   Source source;
   CS linebuf;
   int      linelen;
   Byte   *growbuf;
   int      growbufsiz;
} LocationState;

typedef struct {
    CS namebuf;
    int      bnr;
    CS module;
    CS errmsg;
    int      errmsglen;
    long   lnum;
    long   end_lnum;
    int      col;
    int      end_col;
    Byte   use_viscol;
    CS pattern;
    int enr;
    int type;
    Var* user_data;
    int valid;
} Fields;

declStruct(LocLine);
declStruct(DirStack); 

//Quickfix/Location list definition
//Contains a list of entries (LocLine). first points to the first entry
//and last points to the last entry. count contains the list size.
//
//Usually the list contains one or more entries. But an empty list can be
//created using setqflist()/setloclist() with a title and/or user context
//information and entries can be added later using setqflist()/setloclist().
typedef struct {
   Unt id;      //Unique identifier for this list
   LocLine* first;   //pointer to the first error
   LocLine* last;   //pointer to the last error
   LocLine* curr;   //pointer to the current error
   int count;   //number of errors (0 means empty list)
   int currentIdx;   //current index in the error list
   int noValidEntries;   //true if not a single valid entry found
   int hasUserData; //true if at least one item has user_data attached
   CS title;   //title derived from the command that created
            //the error list or set by setqflist
   Var* qf_ctx;   //context set by setqflist/setloclist
   Callback  textFn;   //'quickfixtextfunc' callback function

   DirStack* dirStack;
   CS dir;
   DirStack* fileStack;
   CS currFName;
   int qf_multiline;
   int qf_multiignore;
   int qf_multiscan;
   long changedTick;
} LocationList;

//:vimgrep command arguments
typedef struct {
   long tomatch;   //maximum number of matches to find
   CS spat;      //search pattern
   Unt flags;      //search modifier
   Arr(CS) fnames;   //list of files to search
   int fcount;      //number of files
   RegMultilineMatch   regmatch;   //compiled search pattern
   CS title;   //quickfix list title
} VimGrepArgs;

declStruct(Sign);


//Specific action on a location list
pub typedef enum {
   LL_ACTION_INVALID, //placeholder for ill-defined strings
   LL_ACTION_ADD, //add entry to location list
   LL_ACTION_REPLACE, 
   LL_ACTION_UPDATE,
   LL_ACTION_NEW, //create new location list
   LL_ACTION_FREE
} LocListAction;

//Sign group
typedef struct signgroup_S {
   int sg_next_sign_id; //next sign id for this group
   Short sg_refcount;   //number of signs in this group
   Boole isPopupOnly;    //is this group for popup portals only?
   Byte sg_name[1];     //sign group name, actually longer
} SignGroup;

struct SignEntry {
   int id;      //unique identifier for each placed sign
   int typeNr;   //typenr of sign
   int priority;   //priority for hiliting
   LineNr lnum;   //line number which has this sign
   SignGroup* group;   //sign group
   SignEntry* next;   //next entry in a list of signs
   SignEntry* prev;   //previous entry -- for easy reordering
};

//Default sign priority for hiliting
pub
#define SIGN_DEF_PRIO   10

typedef struct searchstat {
   int cur;        //current position of found words
   int cnt;        //total count of found words
   int exact_match;//true if matched exactly on specified position
   int incomplete; //0: search was fully completed
                   //1: recomputing was timed out
                   //2: max count exceeded
   int last_maxcount;  //the max count of the last search
} SearchFileStat;

//}}}
#include "h/location.h"
//{{{@@forward declarations
private ArrayList * getTempList(void);
private void clearArrayList(void);
private CS convertErrorFormatToRegex(
   CS efmpat,
   CS regpat,
   ErrorFormatInfo* efminfo,
   int idx,
   int round
);
private CS scanf_fmt_to_regpat(Byte** pefmp, CS efm, int len, CS regpat);
private CS efm_analyze_prefix(CS efmp, ErrorFormatInfo* efminfo);
private int ErrorFormatInfoo_regpat(
   CS efm,
   int len,
   ErrorFormatInfo* fmt_ptr,
   CS regpat
);
private void free_efm_list(ErrorFormatInfo** efm_first);
private int efm_regpat_bufsz(CS efm);
private int efm_option_part_len(Byte *efm);
private ErrorFormatInfo * parse_efm_option(CS efm);
private CS growLineBuffer(LocationState* state, int newsz);
private int nextStringLine(LocationState *state);
private int nextListLine(LocationState* state);
private int nextBufLine(LocationState *state);
private int nextFileLine(LocationState *state);
private inline int getNextLine(LocationState *state);
private int qf_parse_fmt_f(RegMatch* rmp, int midx, Fields* fields, int prefix);
private int qf_parse_fmt_b(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_n(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_l(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_e(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_c(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_k(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_t(RegMatch* rmp, int midx, Fields* fields);
private int copy_nonerror_line(CS linebuf, int linelen, Fields* fields);
private int qf_parse_fmt_m(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_r(RegMatch* rmp, int midx, OUT CS* tail);
private int qf_parse_fmt_p(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_v(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_s(RegMatch* rmp, int midx, Fields* fields);
private int qf_parse_fmt_o(RegMatch* rmp, int midx, Fields* fields);
private int parseErrorFormatMatch(
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmt_ptr,
   RegMatch* regmatch,
   Fields* fields,
   int qf_multiline,
   int qf_multiscan,
   OUT CS* tail
);
private int qf_parse_get_fields(
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmt_ptr,
   Fields* fields,
   int qf_multiline,
   int qf_multiscan,
   OUT CS* tail
);
private int parse_dir_pfx(int idx, Fields* fields, LocationList *ll);
private int parse_file_pfx(
   int idx,
   Fields* fields,
   LocationList* ll,
   CS tail
);
private int qf_parse_line_nomatch(CS linebuf, int linelen, Fields* fields);
private int qf_parse_multiline_pfx(
   int idx,
   LocationList* ll,
   Fields* fields
);
private int qf_parse_line(
   LocationList* ll,
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmtFirst,
   Fields* fields
);
private int isStackEmpty(LocationStack* stack);
private int isEmpty(LocationList* ll);
private int listHasValidEntries(LocationList* ll);
private LocationList * getList(LocationStack* stack, int idx);
private int llAllocateFields(Fields *pfields);
private void freeAList_fields(Fields* pfields);
private int setupState(
   Source source,
   OUT LocationState* locState
);
private void cleanupState(LocationState *locState);
private int processNextLine(LocationList* ll, ErrorFormatInfo* fmtFirst, LocationState* state, Fields* fields);
private int initWorker(
   Source source,
   OUT LocationStack* stack,
   Unt ind,
   CS errorformat,
   Boole newlist,      //true: start a new location list
   CS title
);
private int initAndUpdateTick(
   Source source, OUT LocationStack* stack, NULLABLE CS errorFormat, Boole startNewList, CS title
);
private void storeTitle(LocationList* ll, CS title);
private CS copyCommandTitle(CS cmd);
private LocationList * getCurrent(LocationStack* stack);
private void pop(LocationStack* stack, Boole adjust);
private void push(LocationList newList, LocationStack* stack);
private void newLocList(LocationStack* stack, CS title);
private void locstack_queue_delreq(LocationStack* stack);
private void wipeLlBook(LocationStack* stack);
private void freeAList_list_stack_items(LocationStack* stack);
private void freeAList_lists(LocationStack* stack);
private void ll_free_all(LocationStack** pqi);
private void incrementLlBusyness(void);
private void decrementLlBusyness(void);
private int addEntry(
   LocationList* ll,
   CS dir,      //optional directory name
   CS fname,      //file name or NULL
   CS module,   //module name or NULL
   int bufnum,      //buffer number or zero
   CS mesg,      //message
   long lnum,      //source code line number
   long end_lnum,   //source code end line number
   int col,      //column
   int end_col,   //column for end
   int vis_col,   //using visual column
   CS pattern,   //search pattern
   int nr,      //error number
   int type,      //type character
   Var* user_data,     //custom user data or NULL
   Boole valid      //valid entry
);
private LocationList * allocateLocList(int n);
private LocationStack* identifyStackByLetter(char letter);
private LocationStack* identifyStack(Var* arg);
private LocationStack* identifyStackByInvo(Invocation* invo);
private LocationStack * getStackForCommand(Invocation* invo, int print_emsg);
private int getBookNrForPath(LocationList* ll, CS directory, CS fname);
private CS pushDir(CS dirbuf, DirStack** stackptr, int is_file_stack);
private CS popDir(DirStack **stackptr);
private void qf_clean_dir_stack(DirStack** stackptr);
private CS guessFilepath(LocationList* ll, CS filename);
private int isIdValid(LocationStack* st, Unt id);
private int isEntryPresent(LocationList *ll, LocLine *curr);
private LocLine * getNextValidEntry(LocationList* ll, LocLine* curr, int* currentIdx, Unt dir);
private LocLine * getPrevValidEntry(LocationList* ll, LocLine* curr, int* currentIdx, Unt dir);
private LocLine * get_nth_valid_entry(LocationList* ll, int errornr, Unt dir, int* new_qfidx);
private LocLine * getNthEntry(LocationList* ll, int errornr, int* new_qfidx);
private LocLine * getEntry(LocationList* ll, int errornr, Unt dir, OUT int* new_qfidx);
private Portal * findHelpPortal(void);
private int jumpToHelpPortal(int newPort, int *openedPortal);
private Portal * findPortalIntoLocList_with_normal_buf(void);
private int qf_goto_tabwin_with_file(int fnum);
private int open_new_file_port(LocationStack* llRef);
private void gotoPortalIntoLlFile(Portal* usePort, int fNum);
private void gotoPortalIntoQflFile(int fNum);
private int jumpToUsablePortal(int fNum, int newPort, int* openedPortal);
private int jumpAndEditBook(
   LocationStack* stack,
   LocLine* curr,
   int forceit,
   int prevPortId,
   int* openedPortal
);
private void jumpToEntry(LineNr lNum, int col, Byte visCol, CS pattern);
private void printMsg(
   LocationStack* stack,
   int currentIdx,
   LocLine* curr,
   Book* oldCurBook,
   LineNr old_lnum
);
private int jumpOrOpenPortal(LocationStack* stack, LocLine* curr, int newPort, int* openedPortal);
private int jumpToBook(
   LocationStack* stack,
   int currentIdx,
   LocLine* curr,
   int forceit,
   int prevPortId,
   int* openedPortal,
   int openfold,
   int print_message
);
private void jumpToNewPortal(
   LocationStack* stack,
   Unt dir,
   int errornr,
   Boole forceit,
   Boole newPort
);
private void displayListEntry(LocLine* lline, int ind, int cursel);
private void formatText(ArrayList *gap, CS text);
private void addRangeInformationToArrayList(ArrayList* gap, LocLine* lline);
private void qf_msg(LocationStack* stack, int which, CS lead);
private void freeItems(LocationList* ll);
private void freeAList(LocationList* ll);
private void llAdjustEntries(
   LineNr line1,
   LineNr line2,
   long amount, //how much to adjust entries in [line1; line2]. If == MAXLNUM, lines are deleted
   long amount_after //amount to adjust entries in tail lines (line2; ...)
);
private CS createMsg(int c, int nr);
private void setTitleVar(LocationList* ll);
private int gotoLocationPortal(LocationStack* stack, int resize, int sz, int vertsplit);
private void setPortalOptions();
private int openNewPortal(LocationStack* stack, int height);
private void gotoLine(Portal* po, LineNr lnum);
private Boole updatePortalPos(LocationStack* stack, int      old_currentIdx);
private Boole isLocListPortal(Portal* port, LocationStack* stack);
private Portal * findPortalIntoLocList(LocationStack* stack);
private Book* findLlBook(LocationStack* stack);
private void updateTitleVar(LocationStack* stack);
private void updateBook(LocationStack* stack, LocLine* oldLast);
private inline int addLine(
   Book* book,      //location portal's book
   LineNr   lnum,
   LocLine* lline,
   CS dirname,
   Boole  firstBookLine,
   CS qftf_str
);
private List * callLocListToText(LocationList *ll, int getLlPortalId, long start_idx, long end_idx);
private void fillBookWithLocList(LocationList *ll, Book* book, LocLine *oldLast, int getLlPortalId);
private void updateChangedTick(LocationList* ll);
private int idToNr(LocationStack* stack, Unt listId);
private int restoreList(LocationStack* stack, Unt idSave);
private void jumpToFirstEntry(LocationStack* stack, Unt idSave, Boole forceit);
private NULLABLE CS getGrepAutocommand(CommIndex id);
private Arr(Byte) buildErrorFileName(void);
private CS buildFullShellCommand(CS makecmd);
private int eeglProcessArgs(Invocation* invo, OUT VimGrepArgs* args);
private void makeReceiveMessage(Arr(Byte) msg);
private void makeFinished();
private int nthValidEntry(LocationList* ll, int n, int fdo);
private LocLine * findFirstEntryInBuf(LocationList* ll, int bnr, int* errornr);
private LocLine * qf_find_first_entry_on_line(LocLine* entry, int* errornr);
private LocLine * qf_find_last_entry_on_line(LocLine* entry, int* errornr);
private int isEntryAfterPos(LocLine* lline, Pos* pos, int linewise);
private int qf_entry_before_pos(LocLine *lline, Pos *pos, int linewise);
private int qf_entry_on_or_after_pos(LocLine* lline, Pos* pos, int linewise);
private int isEntryOnOrBeforePos(LocLine* lline, Pos* pos, int linewise);
private LocLine* findEntryAfterPos(
   int      bnr,
   Pos      *pos,
   int      linewise,
   LocLine   *lline,
   int      *errornr
);
private LocLine * findEntryBeforePos(
   int bnr,
   Pos* pos,
   int linewise,
   LocLine* lline,
   int* errornr
);
private LocLine * findClosestEntry(
   LocationList* ll,
   int bnr,
   Pos* pos,
   int dir,
   int linewise,
   OUT int* errornr
);
private void getNthEntryBelow(LocLine *entry_arg, int n, int linewise, int *errornr);
private void getNthEntryAbove(LocLine *entry, int n, int linewise, int *errornr);
private int findNthAdjacentEntry(
   LocationList* ll,
   int bnr,
   Pos* pos,
   int n,
   int dir,
   int linewise
);
private CS cfile_get_auname(CommIndex id);
private CS vgr_get_auname(CommIndex id);
private void vgr_init_regmatch(RegMultilineMatch* regmatch, CS s);
private void vgr_display_fname(Byte *fname);
private Book* vgr_load_dummy_book(CS fname, CS dirname_start, CS dirname_now);
private int vgr_isIdValid(LocationStack* stack, Unt listId, CS title);
private int vgr_match_buflines(
   LocationList* ll,
   CS fname,
   Book* book,
   CS spat,
   RegMultilineMatch* regmatch,
   long* tomatch,
   int duplicate_name,
   int flags
);
private void jumpToFirstMatchAndUpdateDir(
   LocationStack* stack,
   Boole forceit,
   OUT Boole* redrawForDummy,
   OUT Book* firstMatchBook,
   CS target_dir
);
private int vimgrepProcessArgs(Invocation* invo, OUT VimGrepArgs* args);
private int elckGrepFiles(
   LocationStack* stack,
   VimGrepArgs* invos,
   OUT Boole* redrawForDummy,
   OUT Book** firstMatchBook,
   OUT CS* target_dir
);
private void restore_start_dir(CS dirname_start);
private Book* loadDummyBook(
   CS fname,
   CS dirname_start,  //in: old directory
   CS resulting_dir  //out: new directory
);
private void wipeDummyBook(Book* book, CS dirname_start);
private void unloadDummyBook(Book* book, CS dirname_start);
private int get_qfline_items(LocLine *lline, List *list);
private int exportLocList(
   LocationStack* stack,
   Unt ind,
   int entryId,
   OUT List* list
);
private int getList_from_lines(Bag* specifics, DictItem* di, OUT Bag* retBag);
private int getLlPortalId(LocationStack* stack);
private int qf_getprop_qfbufnr(LocationStack* stack, Bag* retBag);
private Unt importKeysFromDict(Bag* specifics);
private int qf_getprop_qfidx(LocationStack* stack, Bag* specifics);
private int getPropertyDefaults(LocationStack* stack, Unt flags, OUT Bag* retBag);
private int qf_getprop_title(LocationList* ll, Bag* retBag);
private int exportToDict(LocationStack* stack, Unt ind, int eidx, OUT Bag* retBag);
private int exportContext(LocationList* ll, OUT Bag* retBag);
private int qf_getprop_idx(LocationList* ll, int eidx, Bag* retBag);
private int qf_getprop_qftf(LocationList* ll, Bag* retBag);
private int getProperties(LocationStack* stack, Bag* specifics, OUT Bag* retBag);
private int addEntry_from_dict(LocationList* ll, Bag* d, int first_entry, int* valid_entry);
private int entry_is_closer_to_target(
   LocLine* entry,
   LocLine* other_entry,
   int target_fnum,
   int target_lnum,
   int target_col
);
private int addEntries(
   OUT LocationStack* stack,
   Unt ind,
   List* list,
   CS title,
   LocListAction action
);
private Unt qf_setprop_get_qfidx(
   LocationStack* stack,
   Bag* specifics,
   LocListAction action,
   OUT Boole* newlist
);
private int setTitle(LocationStack* stack, Unt ind, Bag* specifics, DictItem* di);
private int setItems(LocationStack* stack, Unt ind, DictItem* di, LocListAction action);
private int setLinesFromList(
   LocationStack* stack,
   Unt ind,
   Bag* specifics,
   DictItem* di,
   LocListAction action
);
private int setContext(LocationList* ll, DictItem* di);
private int setCurrentIndex(LocationStack *stack, LocationList *ll, DictItem *di);
private int setTextFn(LocationList *ll, DictItem *di);
private int setProperties(LocationStack *stack, Bag *specifics, LocListAction action, CS title);
private void freeTheStack(LocationStack* stack);
private Boole checkIfUserDataLocked(LocationStack* stack, int copyID);
private Boole checkIfContextAndCallbackLocked(LocationStack* stack, int copyID);
private Boole markReferencesInStack(LocationStack* st, int copyId);
private inline Arr(Byte) getAutocmdNameForCbuffer(CommIndex id);
private int processCbookArgs(Invocation* invo, OUT Book** outBook, LineNr* line1, LineNr* line2);
private void searchInFile(
   LocationList *ll,
   CS fname,
   OUT RegMatch *p_regmatch
);
private void searchFilesInDir(LocationList* ll, CS dirname, OUT RegMatch* p_regmatch, CS lang);
private void setLocationListInternal(
   LocationStack* stack,
   Var* listArg,
   Var* actionArg,
   Var* specificArg,
   Var* returnVar
);
private void fname2fnum(FileMarkExt* fm);
private void fmarks_check_one(FileMarkExt* fm, CS name, Book* book);
private CS mark_line(Pos* mp, int lead_len);
private void show_one_mark(
   int c,
   CS arg,
   Pos* p,
   CS name_arg,
   int current   //in current file
);
private int add_mark(List* l, CS mname, Pos* pos, int bufnr, CS fname);
private void get_buf_local_marks(Book *book, List *l);
private void get_global_marks(List *l);
private SignGroup * sign_group_ref(CS groupname);
private void sign_group_unref(CS groupname);
private int sign_in_group(SignEntry *sign, CS group);
private Boole signIsVisible(SignEntry* sign, Portal* po);
private int sign_group_get_next_signid(Book *book, CS groupname);
private void insert_sign(
   Book *book, //buffer to store sign in
   SignEntry* prev, //previous sign entry
   SignEntry* next, //next sign entry
   int id, //sign ID
   CS group, //sign group; NULL for global group
   int prio, //sign priority
   LineNr lnum, //line number which gets the mark
   int typenr
);
private void insert_sign_by_lnum_prio(
   Book *book, //buffer to store sign in
   SignEntry *prev, //previous sign entry
   int id, //sign ID
   Byte *group, //sign group; NULL for global group
   int prio, //sign priority
   LineNr lnum, //line number which gets the mark
   int typenr
);
private Sign * find_sign_by_typenr(int typenr);
private CS sign_typenr2name(int typenr);
private Bag* sign_get_info(SignEntry* sign);
private void sign_sort_by_prio_on_line(Book *book, SignEntry *sign);
private void addSignToBook(
   Book* book, //book to store sign in
   int id, //sign ID
   CS groupname, //sign group
   int prio, //sign priority
   LineNr lnum, //line number which gets the mark
   int typenr //typenr of sign we are adding
);
private LineNr changeSignType(
   Book* book, //book to store sign in
   int markId, //sign ID
   CS group, //sign group
   int typenr, //typenr of sign we are adding
   int prio //sign priority
);
private LineNr delsign(Book* book, //buffer sign is stored in
            LineNr atlnum, //sign at this line, 0 - at any line
            int id, //sign id
            Byte *group) //sign group
;
private int buf_findsign(Book *book, //buffer to store sign in
             int id, //sign ID
             CS group) //sign group
;
private SignEntry * getsignAtLine(Book* book, //book whose sign we are searching for
              LineNr lnum, //line number of sign
              CS groupname //sign group name
);
private int findsign_id(Book* book, //book whose sign we are searching for
            LineNr lnum, //line number of sign
            CS groupname //sign group name
);
private void sign_list_placed(Book* rbook, CS sign_group);
private void sign_mark_adjust(
    LineNr line1,
    LineNr line2,
    long amount,
    long amount_after
);
private int sign_cmd_idx(CS begin_cmd, //begin of sign subcmd
             CS end_cmd //just after sign subcmd
);
private Sign* sign_find(CS name, Sign** sp_prev);
private Sign * alloc_new_sign(CS name);
private int sign_define_init_text(Sign *sp, Byte *text);
private void sign_list_by_name(Byte *name);
private void may_force_numberwidth_recompute(Book* book, int unplace);
private int sign_unplace(int sign_id, Byte *sign_group, Book* book, LineNr atlnum);
private void sign_unplace_at_cursor(CS groupname);
private LineNr sign_jump(int sign_id, Byte *sign_group, Book* book);
private void sign_define_cmd(Byte *sign_name, Byte *cmdline);
private void sign_place_cmd(
   Book* book,
   LineNr lnum,
   CS sign_name,
   int id,
   CS group,
   int prio
);
private void sign_unplace_cmd(Book* book, LineNr lnum, CS sign_name, int id, CS group);
private void sign_jump_cmd(
   Book* book,
   LineNr lnum,
   CS sign_name,
   int id,
   CS group
);
private int parse_sign_cmd_args(
   int cmd,
   CS arg,
   OUT CS* sign_name,
   int* signid,
   Byte** group,
   int *prio,
   Book** book,
   LineNr* lnum
);
private void sign_getinfo(Sign* sp, Bag* retBag);
private void sign_getlist(CS name, List* retlist);
private void getSignsInBook(
   Book* book,
   LineNr lnum,
   int sign_id,
   CS sign_group,
   List* retlist
);
private void sign_get_placed(
   Book* book,
   LineNr lnum,
   int sign_id,
   CS sign_group,
   List* retlist
);
private void sign_list_defined(Sign* sp);
private void sign_undefine(Sign* sp, Sign* sp_prev);
private CS get_nth_sign_name(int idx);
private CS get_nth_sign_group_name(int idx);
private int sign_define_from_dict(CS name_arg, Bag* bag);
private void sign_define_multiple(List* l, List* retlist);
private int sign_place_from_dict(
   Var* id_tv,
   Var* group_tv,
   Var* name_tv,
   Var* buf_tv,
   Bag* dict
);
private void sign_undefine_multiple(List *l, List *retlist);
private int sign_unplace_from_dict(Var *group_tv, Bag *dict);
private NULLABLE SignEntry * get_first_valid_sign(Portal *wp);
private void save_incsearch_state(void);
private void restore_incsearch_state(void);
private int first_submatch(RegMultilineMatch *rp);
private int check_prevcol(
   CS linep,
   int      col,
   int      ch,
   int      *prevcol
);
private int find_rawstring_end(CS linep, Pos* startpos, Pos* endpos);
private void find_mps_values(
   OUT Unt* initc,
   OUT Unt* findc,
   OUT int* backwards,
   int switchit
);
private int is_zero_width(
   Text pattern,
   Boole move,
   Pos* cur,
   Unt direction
);
private void cmdline_search_stat(
   int dirc,
   Pos* pos,
   Pos* cursor_pos,
   int show_top_bot_msg,
   CS msgbuf,
   Unt msgbuflen,
   int recompute,
   int maxcount,
   long timeout
);
private void update_search_stat(
   int dirc,
   Pos* pos,
   Pos* cursor_pos,
   SearchFileStat* stat,
   int recompute,
   int maxcount,
   long timeout
);
private CS get_line_and_copy(LineNr lnum, CS buf);
private void show_pat_in_path(
   CS  line,
   int       type,
   int       did_show,
   int       action,
   FILE* fp,
   LineNr* lnum,
   long    count
);
private int match_add(
   Portal* po,
   CS grp,
   CS pat,
   int prio,
   int id,
   List* pos_list
);
private int match_delete(Portal* po, int id, int perr);
private MatchItem* get_match(Portal* po, int id);
private int next_search_hl_pos(
   OUT Match* match,   //points to a match
   LineNr lnum,
   MatchItem* matchItem,   //match item with positions
   ColNr mincol   //minimal column for a match
);
private void next_search_hl(
   Portal* port,
   Match* search_hl,
   Match* match,   //points to search_hl or a match
   LineNr lnum,
   ColNr mincol,   //minimal column for a match
   MatchItem* cur   //to retrieve match positions if any
);
private void check_cur_search_hl(Portal* po, Match* match);
private int matchadd_dict_arg(Var* tv, OUT Portal** port);
private int helpCompare(const void *s1, const void *s2);
private void generateHelpTagsForDir(
   CS dir,              //doc directory
   CS ext,              //suffix, ".txt", ".itx", ".frx", etc.
   CS tagfname,         //"tags" for English, "tags-fr" for French.
   int add_help_tags,   //add "help-tags" tag
   int ignore_writeerr  //ignore write error
);
private void do_helptags(CS dirname, int add_help_tags, int ignore_writeerr);
private void helptagsCb(CS fname, void* cookie);
//}}}
//{{{location lists

struct DirStack {
   DirStack* next;
   CS dirname;
};

#define FORWARD_FILE    3
#define BACKWARD_FILE   7
#define STACK_CAPACITY 20

//For each error the next struct is allocated and linked in a list.
struct LocLine {
   LocLine* next;   //pointer to next error in the list
   LocLine* prev;   //pointer to previous error in the list
   LineNr lNum;   //line number where the error occurred
   LineNr endLNum;   //line number when the error has range or zero
   int fNum;   //file number for the line
   int col;      //column where the error occurred
   int endCol;   //column when the error has range or zero
   int errNum;      //error number
   Arr(Byte) moduleName;   //module name for this error
   Arr(Byte) fName;   //different filename if there're hard links
   Arr(Byte) pattern;   //search pattern for the error
   Arr(Byte) text;   //description of the error
   Byte visCol;   //set to true if col and endCol is screen column
   Byte isCleared;   //set to true if line has been deleted
   Byte kind;   //type of the error (mostly 'E'); 1 for :helpgrep
   Var userData;   //custom user data associated with this item
   Byte isValid;   //valid error message detected
};

//There is a stack of location lists.
#define INVALID_LL_IND (3000000000)
#define INVALID_LL_BUFNR (0)

//Quickfix/Location list stack definition. Contains a list of location lists (LocationList)
struct LocationStack {
   //Count of references to this list. Used only for location lists.
   //When a location list portal reference this list, refcount
   //will be 2. Otherwise, refcount will be 1. When refcount
   //reaches 0, the list is freed.
   Unt refCount;
   Unt listcount;       //current number of lists
   Unt currList;       //current error list
   Unt cap;        //maximum number of lists
   Arr(LocationList) lists;
   int bufNum;       //location portal's book number
};

private LocationStack *mainStackG;   //points to mainStackG_actual if memory allocation is successful.

private LocationStack locationStacksP[COUNT_LOC_LISTS];
private Unt lastUsedLlIdS = 0;   //Last used location list id

private Boole isMakeRunningS = false; //if a "make" job is in progress, then listId, else -1
private List* makeInProgressS; //the list of messages from a running "make" command


#define FMT_PATTERNS 14      //maximum number of % recognized


//Structure used to hold the info of one part of 'errorformat'
struct ErrorFormatInfo {
    RegProg* prog;   //pre-formatted part of 'errorformat'
    ErrorFormatInfo* next;   //pointer to next (NULL if last)
    Byte addr[FMT_PATTERNS]; //indices of used % patterns
    Byte prefix;   //prefix of this format line:
            //  'D' enter directory
            //  'X' leave directory
            //  'A' start of multi-line message
            //  'E' error message
            //  'W' warning message
            //  'I' informational message
            //  'N' note message
            //  'C' continuation line
            //  'Z' end of multi-line message
            //  'G' general, unspecific message
            //  'P' push file (partial) message
            //  'Q' pop/quit file (partial) message
            //  'O' overread (partial) message
    Byte flags;   //additional flags given in prefix
            //  '-' do not include this line
            //  '+' include whole line in message
    int conthere;   //%> used
};

//List of location lists to be deleted.
//Used to delay the deletion of locations lists by autocmds.
typedef struct DeletionList DeletionList;
struct DeletionList {
    DeletionList* next;
    LocationStack      *stack;
};

private DeletionList* deletionListG = NULL;

//Counter to prevent autocmds from freeing up location lists when they are
//still being used.
private int qfBusynessG = 0;

private ErrorFormatInfo   *fmt_start = NULL; //cached across qf_parse_line() calls

//callback function for 'quickfixtextfunc'
private Callback locationTextFnS;

private void push(LocationList newList, LocationStack* stack);
private void newLocList(LocationStack *stack, Byte *title);
private int addEntry(
      LocationList* ll, Byte* dir, Byte *fname, Byte *module, int bufnum, 
      Byte *mesg, long lnum, long end_lnum, int col, int end_col, int vis_col, 
      Byte *pattern, int nr, int type, Var *user_data, Boole valid
);
private void freeAList(LocationList *ll);
private CS createMsg(int, int);
private int   getBookNrForPath(LocationList *ll, Byte *, Byte *);
private Arr(Byte) pushDir(Byte *, DirStack **, int is_file_stack);
private CS popDir(DirStack **);
private CS guessFilepath(LocationList *ll, Byte *);
private void jumpToNewPortal(LocationStack *stack, Unt dir, int errornr, Boole forceit, Boole newPort
);
private void   formatText(ArrayList *gap, Byte *text);
private void   addRangeInformationToArrayList(ArrayList *gap, LocLine *lline);
private Boole   updatePortalPos(LocationStack *stack, int old_currentIdx);
private Portal   *findPortalIntoLocList(LocationStack *stack);
private Book   *findLlBook(LocationStack *stack);
private void   updateBook(LocationStack *stack, LocLine *oldLast);
private void fillBookWithLocList(LocationList *ll, Book* book, LocLine *oldLast, int getLlPortalId);
private Book   *loadDummyBook(Byte *fname, Byte *dirname_start, Byte *resulting_dir);
private void   wipeDummyBook(Book *book, Byte *dirname_start);
private void   unloadDummyBook(Book *book, Byte *dirname_start);
private int   entry_is_closer_to_target(
      LocLine *entry, LocLine *other_entry, int target_fnum, int target_lnum, int target_col
);
private Arr(Byte) vgr_get_auname(CommIndex id);

private int vimgrepProcessArgs(Invocation* invo, OUT VimGrepArgs* args);
private int elckGrepFiles(LocationStack*, VimGrepArgs*, OUT Boole*, OUT Book**, OUT CS*);
private void vgr_init_regmatch(RegMultilineMatch *regmatch, Byte *s);
private void updateChangedTick(LocationList *ll);

private LocationList* getCurrent(LocationStack *stack);

private void
jumpToFirstMatchAndUpdateDir(
   LocationStack* stack,
   Boole forceit,
   OUT Boole* redrawForDummy,
   OUT Book* firstMatchBook,
   CS target_dir);

//Quickfix portal check helper macro
#define isLocListPortalDOW(wp) (isLocationListBook((wp)->book) && (wp)->locationStackRef == NULL)
//Location list portal check helper macro
#define IS_LL_PORTAL(wp) (isLocationListBook((wp)->book) && (wp)->locationStackRef != NULL)

//Return location list for portal 'wp'
//For location list portal, return the referenced location list
#define GET_LOC_LIST(wp) (IS_LL_PORTAL(wp) ? (wp)->locationStackRef : NULL)

//Macro to loop through all the items in a quickfix list
//Quickfix item index starts from 1, so i below starts at 1
#define FOR_ALL_LL_ITEMS(ll, lline, i) \
          for (i = 1, lline = ll->first; \
             !gotInterruptG && i <= ll->count && lline != NULL; \
             ++i, lline = lline->next)

//Looking up a book can be slow if there are many.  Remember the last one
//to make this a lot faster if there are multiple matches in the same file.
private CS lastBookNameS = NULL;
private BookRef  last_bufref = {NULL, 0, 0};

private ArrayList tempList;


//Get a growarray to buffer text in.  Shared between various commands to avoid many alloc/free calls.
private ArrayList *
getTempList(void) {
   static int initialized = false;

   if (!initialized) {
      initialized = true;
      ga_init2(&tempList, 1, 256);
   }

   //Reset the length to zero.  Retain c from previous use to avoid many alloc/free calls.
   tempList.len = 0;

   return &tempList;
}

//The "tempList" arraylist buffer is reused across multiple loclist commands as
//a temporary buffer to reduce the number of alloc/free calls.  But if the
//buffer size is large, then to avoid holding on to that memory, clear the
//grow array.  Otherwise just reset the grow array length.
private void
clearArrayList(void) {
   if (tempList.cap > 1000)
      ga_clear(&tempList);
   else
      tempList.len = 0;
}

//Maximum number of bytes allowed per line while reading a errorfile.
#define LINE_MAXLEN 4096

//Patterns used.  Keep in sync with parseFormats[].
struct fmtpattern{
   CS pattern;
   Byte   convchar;
} FORMAT_PATTERNS[FMT_PATTERNS] = { SMAP1((CS),
   ".\\+", 'f',      //only used when at end
   "\\d\\+", 'b',    //1
   "\\d\\+", 'n',    //2
   "\\d\\+", 'l',    //3
   "\\d\\+", 'e',    //4
   "\\d\\+", 'c',    //5
   "\\d\\+", 'k',    //6
   ".", 't',         //7
#define FMT_PATTERN_M 8
   ".\\+", 'm',      //8
#define FMT_PATTERN_R 9
   ".*", 'r',        //9
   "[-    .]*", 'p', //10
   "\\d\\+", 'v',    //11
   ".\\+", 's',      //12
   ".\\+", 'o'       //13
)};

//Convert an errorformat pattern to a regular expression pattern.
//See FORMAT_PATTERNS definition above for the list of supported patterns.  The
//pattern specifier is supplied in "efmpat".  The converted pattern is stored
//in "regpat".  Returns a pointer to the location after the pattern.
private CS
convertErrorFormatToRegex(
   CS efmpat,
   CS regpat,
   ErrorFormatInfo* efminfo,
   int idx,
   int round
){
   CS srcptr;

   if (efminfo->addr[idx]) {
      //Each errorformat pattern can occur only once
      showErrFmtMsg(_(e_too_many_chr_in_format_string), *efmpat);
      return NULL;
   }
   if ((idx && idx < FMT_PATTERN_R && firstOccurrence(S"DXOPQ", efminfo->prefix) != NULL)
       || (idx == FMT_PATTERN_R && firstOccurrence(S"OPQ", efminfo->prefix) == NULL)
   ) {
      showErrFmtMsg(_(e_unexpected_chr_in_format_str), *efmpat);
      return NULL;
   }
   efminfo->addr[idx] = (Byte)++round;
   *regpat++ = '\\';
   *regpat++ = '(';
   if (*efmpat == 'f' && efmpat[1] != ZERO) {
      if (efmpat[1] != '\\' && efmpat[1] != '%') {
         //A file name may contain spaces, but this isn't
         //in "\f".  For "%f:%l:%m" there may be a ":" in
         //the file name.  Use ".\{-1,}x" instead (x is
         //the next character), the requirement that :999:
         //follows should work.
         STRCPY(regpat, ".\\{-1,}");
         regpat += 7;
      } else {
          //File name followed by '\\' or '%': include as
          //many file name chars as possible.
          STRCPY(regpat, "\\f\\+");
          regpat += 4;
      }
   } else {
      srcptr = FORMAT_PATTERNS[idx].pattern;
      while ((*regpat = *srcptr++) != ZERO)
          ++regpat;
   }
   *regpat++ = '\\';
   *regpat++ = ')';

   return regpat;
}

//Convert a scanf-like format in 'errorformat' to a regular expression.
//Return a pointer to the location after the pattern.
private CS
scanf_fmt_to_regpat(Byte** pefmp, CS efm, int len, CS regpat) {
   CS efmp = *pefmp;

   if (*efmp == '[' || *efmp == '\\') {
      if ((*regpat++ = *efmp) == '[')   { //%*[^a-z0-9] etc.
         if (efmp[1] == '^')
            *regpat++ = *++efmp;
         if (efmp < efm + len) {
            *regpat++ = *++efmp;       //could be ']'
         while (efmp < efm + len && (*regpat++ = *++efmp) != ']')
             //skip ;
         if (efmp == efm + len) {
             emsg(_(e_missing_rsb_in_format_string));
             return NULL;
         }
         }
      } ei (efmp < efm + len)   //%*\D, %*\s etc.
         *regpat++ = *++efmp;
      *regpat++ = '\\';
      *regpat++ = '+';
   } else {
      //TODO: scanf()-like: %*ud, %*3c, %*f, ... ?
      showErrFmtMsg(_(e_unsupported_chr_in_format_string), *efmp);
         return NULL;
   }

   *pefmp = efmp;

   return regpat;
}

//Analyze/parse an errorformat prefix.
private CS
efm_analyze_prefix(CS efmp, ErrorFormatInfo* efminfo){
   if (firstOccurrence((CS)"+-", *efmp) != NULL)
      efminfo->flags = *efmp++;
   if (firstOccurrence(S"DXAEWINCZGOPQ", *efmp) != NULL)
      efminfo->prefix = *efmp;
   else {
      showErrFmtMsg(_(e_invalid_chr_in_format_string_prefix), *efmp);
      return NULL;
   }

   return efmp;
}

//Convert a 'errorformat' string part in 'efm' to a regular expression
//pattern. The resulting regex pattern is returned in "regpat". Additional
//information about the 'erroformat' pattern is returned in "fmt_ptr". Return OK or FAIL.
private int
ErrorFormatInfoo_regpat(
   CS efm,
   int len,
   ErrorFormatInfo* fmt_ptr,
   CS regpat
){
   Byte* efmp;
   Unt idx = 0;

   //Build a regexp pattern for a 'errorformat' option part
   CS ptr = regpat;
   *ptr++ = '^';
   int round = 0;
   for (efmp = efm; efmp < efm + len; ++efmp) {
      if (*efmp == '%') {
         ++efmp;
         for (idx = 0; idx < FMT_PATTERNS; ++idx) {
            if (FORMAT_PATTERNS[idx].convchar == *efmp)
               break;
         } 
         if (idx < FMT_PATTERNS) {
            ptr = convertErrorFormatToRegex(efmp, ptr, fmt_ptr, idx, round);
            if (ptr == NULL)
                return FAIL;
            round++;
         } ei (*efmp == '*') {
         ++efmp;
         ptr = scanf_fmt_to_regpat(&efmp, efm, len, ptr);
         if (ptr == NULL)
             return FAIL;
         } ei (firstOccurrence((CS)"%\\.^$~[", *efmp) != NULL)
            *ptr++ = *efmp;      //regexp magic characters
         ei (*efmp == '#')
            *ptr++ = '*';
         ei (*efmp == '>')
            fmt_ptr->conthere = true;
         ei (efmp == efm + 1) {     //analyse prefix
            //prefix is allowed only at the beginning of the errorformat
            //option part
            efmp = efm_analyze_prefix(efmp, fmt_ptr);
            if (efmp == NULL)
                return FAIL;
         } else {
            showErrFmtMsg(_(e_invalid_chr_in_format_string), *efmp);
            return FAIL;
         }
      } else {        //copy normal character
         if (*efmp == '\\' && efmp + 1 < efm + len)
            ++efmp;
         ei (firstOccurrence((CS)".*^$~[", *efmp) != NULL)
            *ptr++ = '\\';   //escape regexp atoms
         if (*efmp)
            *ptr++ = *efmp;
      }
   }
   *ptr++ = '$';
   *ptr = ZERO;

   return OK;
}

//Free the 'errorformat' information list
private void
free_efm_list(ErrorFormatInfo** efm_first){
   for (ErrorFormatInfo* efm_ptr = *efm_first; efm_ptr; efm_ptr = *efm_first) {
      *efm_first = efm_ptr->next;
      eeRegFree(efm_ptr->prog);
      eeglFree(efm_ptr);
   }
   fmt_start = NULL;
}

//Compute the size of the buffer used to convert a 'errorformat' pattern into
//a regular expression pattern.
private int
efm_regpat_bufsz(CS efm){
   int sz = (FMT_PATTERNS * 3) + ((int)STRLEN(efm) << 2);
   for (int i = FMT_PATTERNS; i > 0; )
      sz += (int)STRLEN(FORMAT_PATTERNS[--i].pattern);
   sz += 2; //"%f" can become two chars longer

   return sz;
}

//Return the length of a 'errorformat' option part (separated by ",").
private int
efm_option_part_len(Byte *efm){
   int len;

   for (len = 0; efm[len] != ZERO && efm[len] != ','; ++len) {
      if (efm[len] == '\\' && efm[len + 1] != ZERO)
          ++len;
   } 

   return len;
}

//Parse the 'errorformat' option. Multiple parts in the 'errorformat' option
//are parsed and converted to regular expressions. Returns information about
//the parsed 'errorformat' option.
private ErrorFormatInfo *
parse_efm_option(CS efm){
   ErrorFormatInfo* fmt_ptr = NULL;
   ErrorFormatInfo* fmtFirst = NULL;
   ErrorFormatInfo* fmt_last = NULL;
   CS fmtstr = NULL;
   int len;

   //Each part of the format string is copied and modified from errorformat
   //to regex prog.  Only a few % characters are allowed.

   //Get some space to modify the format string into.
   int sz = efm_regpat_bufsz(efm);
   if ((fmtstr = alloc_id(sz, aid_ll_efm_fmtstr)) == NULL)
      goto parse_efm_error;

   while (efm[0] != ZERO) {
      //Allocate a new eformat structure and put it at the end of the list
      fmt_ptr = ALLOC_CLEAR_ONE_ID(ErrorFormatInfo, aid_ll_efm_fmtpart);
      if (fmt_ptr == NULL)
         goto parse_efm_error;
      if (fmtFirst == NULL)       //first one
         fmtFirst = fmt_ptr;
      else
         fmt_last->next = fmt_ptr;
      fmt_last = fmt_ptr;

      //Isolate one part in the 'errorformat' option
      len = efm_option_part_len(efm);

      if (ErrorFormatInfoo_regpat(efm, len, fmt_ptr, fmtstr) == FAIL)
          goto parse_efm_error;
      if ((fmt_ptr->prog = compileRegexp(fmtstr, RE_MAGIC + RE_STRING)) == NULL)
          goto parse_efm_error;
      //Advance to next part
      efm = skip_to_option_part(efm + len);   //skip comma and spaces
   }

   if (fmtFirst == NULL)   //nothing found
      emsg(_(e_errorformat_contains_no_pattern));

   goto parse_efm_end;

parse_efm_error:
   free_efm_list(&fmtFirst);

parse_efm_end:
   eeglFree(fmtstr);

   return fmtFirst;
}

enum {
   QF_FAIL = 0,
   QF_OK = 1,
   QF_END_OF_INPUT = 2,
   QF_NOMEM = 3,
   QF_IGNORE_LINE = 4,
   QF_MULTISCAN = 5,
   QF_ABORT = 6
};

//Allocate more memory for the line buffer used for parsing lines.
private CS
growLineBuffer(LocationState* state, int newsz) {
   CS  p;

   //If the line exceeds LINE_MAXLEN exclude the last
   //byte since it's not a NL character.
   state->linelen = newsz > LINE_MAXLEN ? LINE_MAXLEN - 1 : newsz;
   if (state->growbuf == NULL) {
      state->growbuf = alloc_id(state->linelen + 1, aid_ll_linebuf);
      if (state->growbuf == NULL)
         return NULL;
      state->growbufsiz = state->linelen;
   } ei (state->linelen > state->growbufsiz) {
      p = eeRealloc(state->growbuf, state->linelen + 1);
      state->growbuf = p;
      state->growbufsiz = state->linelen;
   }
   return state->growbuf;
}

//Get the next string (separated by newline) from a string source
private int
nextStringLine(LocationState *state) {
   //Get the next line from the supplied string
   CS inputString = state->source.String.c;
   if (*inputString== ZERO) //Reached the end of the string
      return QF_END_OF_INPUT;

   CS p = firstOccurrence(inputString, '\n');
   int len = p ? (int)(p - inputString) + 1 : (int)STRLEN(inputString);

   if (len > IOSIZE - 2) {
      state->linebuf = growLineBuffer(state, len);
      if (state->linebuf == NULL)
         return QF_NOMEM;
   } else {
      state->linebuf = IObuff;
      state->linelen = len;
   }
   copySubstrToAllocation(state->linebuf, (Text){inputString, state->linelen});

   //Increment using len in order to discard the rest of the
   //line if it exceeds LINE_MAXLEN.
   inputString += len;
   state->source.String.c = inputString;

   return QF_OK;
}

//Get the next string from the List items
private int
nextListLine(LocationState* state) {
   ListItem* listItem = state->source.List.c;

   while (listItem != NULL && (listItem->c.tag != VAR_STRING || listItem->c.string == NULL))
      listItem = listItem->next;   //Skip non-string items

   if (listItem == NULL) {     //End of the list
      state->source.List.c = NULL;
      return QF_END_OF_INPUT;
   }

   int len = (int)STRLEN(listItem->c.string);
   if (len > IOSIZE - 2) {
      state->linebuf = growLineBuffer(state, len);
      if (state->linebuf == NULL)
         return QF_NOMEM;
   } else {
      state->linebuf = IObuff;
      state->linelen = len;
   }

   copySubstrToAllocation(state->linebuf, (Text){listItem->c.string, state->linelen});

   state->source.List.c = listItem->next;   //next item
   return QF_OK;
}

//Get the next string from state->buf.
private int
nextBufLine(LocationState *state) {
   //Get the next line from the supplied buffer
  
   if (state->source.Book.start >= state->source.Book.end)
      return QF_END_OF_INPUT;

   CS p_buf = memGetLine(state->source.Book.c, state->source.Book.start, false);
   int len = memGetBookLen(state->source.Book.c, state->source.Book.start);
   state->source.Book.start++;

   if (len > IOSIZE - 2) {
      state->linebuf = growLineBuffer(state, len);
      if (state->linebuf == NULL)
         return QF_NOMEM;
   } else {
      state->linebuf = IObuff;
      state->linelen = len;
   }
   copySubstrToAllocation(state->linebuf, (Text){p_buf, state->linelen});

   return QF_OK;
}

//Get the next string when source = file.
private int
nextFileLine(LocationState *state) {
   if (fgets((char *)IObuff, IOSIZE, state->source.File.c) == NULL)
      return QF_END_OF_INPUT;

   Boole discard = false;
   state->linelen = (int)STRLEN(IObuff);
   if (state->linelen == IOSIZE - 1 && !(IObuff[state->linelen - 1] == '\n')) {
   
      //The current line exceeds IObuff, continue reading using
      //growbuf until EOL or LINE_MAXLEN bytes is read.
      if (state->growbuf == NULL) {
         state->growbufsiz = 2 * (IOSIZE - 1);
         state->growbuf = alloc_id(state->growbufsiz, aid_ll_linebuf);
         if (state->growbuf == NULL)
            return QF_NOMEM;
      }

      //Copy the read part of the line, excluding null-terminator
      memcpy(state->growbuf, IObuff, IOSIZE - 1);
      int growbuflen = state->linelen;

      for (;;) {
         if (fgets(
                  (char *)state->growbuf + growbuflen, 
                  state->growbufsiz - growbuflen, 
                  state->source.File.c) == NULL
         ) {
            break;
         } 
         state->linelen = (int)STRLEN(state->growbuf + growbuflen);
         growbuflen += state->linelen;
         if ((state->growbuf)[growbuflen - 1] == '\n')
            break;
         if (state->growbufsiz == LINE_MAXLEN) {
            discard = true;
            break;
         }

         state->growbufsiz = 2 * state->growbufsiz < LINE_MAXLEN 
            ? 2 * state->growbufsiz : LINE_MAXLEN;
         Arr(Byte) p;
         p = eeRealloc(state->growbuf, state->growbufsiz);
         state->growbuf = p;
     }

      while (discard) {
         //The current line is longer than LINE_MAXLEN, continue
         //reading but discard everything until EOL or EOF is reached.
         if (fgets((char *)IObuff, IOSIZE, state->source.File.c) == NULL
                || (int)STRLEN(IObuff) < IOSIZE - 1
                || IObuff[IOSIZE - 2] == '\n') {
            break;
         } 
      }

      state->linebuf = state->growbuf;
      state->linelen = growbuflen;
   } else
      state->linebuf = IObuff;

    return QF_OK;
}

//Get the next string from the source
private inline int
getNextLine(LocationState *state) {
   int status = QF_FAIL;
   switch (state->source.tag) {
   case SOURCE_FILE: {
      status = nextFileLine(state);
      break;
   }
   case SOURCE_BOOK: {
      status = nextBufLine(state);
      break;
   }
   case SOURCE_STRING: {
      status = nextStringLine(state);
      break;
   }
   case SOURCE_LIST: {
      status = nextListLine(state);
      break;
   }
   case SOURCE_FILENAME: break;
   }

   if (status != QF_OK)
      return status;

   //remove newline/CR from the line
   if (state->linelen > 0 && state->linebuf[state->linelen - 1] == '\n') {
      state->linebuf[state->linelen - 1] = ZERO;
   }

   return QF_OK;
}

//Parse the match for filename ('%f') pattern in regmatch.
//Return the matched value in "fields->namebuf".
private int
qf_parse_fmt_f(RegMatch* rmp, int midx, Fields* fields, int prefix) {

   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;

   //Expand ~/file and $HOME/file to full path.
   int c = *rmp->endp[midx];
   *rmp->endp[midx] = ZERO;
   doExpandEnv(OUT (Text){fields->namebuf, CMDBUFFSIZE}, rmp->startp[midx]);
   *rmp->endp[midx] = c;

   //For separate filename patterns (%O, %P and %Q), the specified file should exist.
   if (firstOccurrence(S"OPQ", prefix) != NULL && mch_getperm(fields->namebuf) == -1)
      return QF_FAIL;

   return QF_OK;
}

//Parse the match for buffer number ('%b') pattern in regmatch.
//Return the matched value in "fields->bnr".
private int
qf_parse_fmt_b(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   int bnr = (int)atol((char *)rmp->startp[midx]);
   if (bookFindFileByBookNr(bnr) == NULL)
      return QF_FAIL;
   fields->bnr = bnr;
   return QF_OK;
}

//Parse the match for error number ('%n') pattern in regmatch.
//Return the matched value in "fields->enr".
private int
qf_parse_fmt_n(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->enr = (int)atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for line number ('%l') pattern in regmatch.
//Return the matched value in "fields->lnum".
private int
qf_parse_fmt_l(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->lnum = atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for end line number ('%e') pattern in regmatch.
//Return the matched value in "fields->end_lnum".
private int
qf_parse_fmt_e(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->end_lnum = atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for column number ('%c') pattern in regmatch.
//Return the matched value in "fields->col".
private int
qf_parse_fmt_c(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->col = (int)atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for end column number ('%k') pattern in regmatch.
//Return the matched value in "fields->end_col".
private int
qf_parse_fmt_k(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->end_col = (int)atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for error type ('%t') pattern in regmatch.
//Return the matched value in "fields->type".
private int
qf_parse_fmt_t(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->type = *rmp->startp[midx];
   return QF_OK;
}

//Copy a non-error line into the error string. Return the matched line in "fields->errmsg".
private int
copy_nonerror_line(CS linebuf, int linelen, Fields* fields) {
   if (linelen >= fields->errmsglen) {
      //linelen + null terminator
      CS p = eeRealloc(fields->errmsg, linelen + 1);
      fields->errmsg = p;
      fields->errmsglen = linelen + 1;
   }
   //copy whole line to error message
   copySubstrToAllocation(fields->errmsg, (Text){linebuf, linelen});

   return QF_OK;
}

//Parse the match for error message ('%m') pattern in regmatch.
//Return the matched value in "fields->errmsg".
private int
qf_parse_fmt_m(RegMatch* rmp, int midx, Fields* fields) {
   CS p;

   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   int len = (int)(rmp->endp[midx] - rmp->startp[midx]);
   if (len >= fields->errmsglen) {
      //len + null terminator
      p = eeRealloc(fields->errmsg, len + 1);
      fields->errmsg = p;
      fields->errmsglen = len + 1;
   }
   copySubstrToAllocation(fields->errmsg, (Text){rmp->startp[midx], len});
   return QF_OK;
}

//Parse the match for rest of a single-line file message ('%r') pattern.
//Return the matched value in "tail".
private int
qf_parse_fmt_r(RegMatch* rmp, int midx, OUT CS* tail) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   *tail = rmp->startp[midx];
   return QF_OK;
}

//Parse the match for the pointer line ('%p') pattern in regmatch.
//Return the matched value in "fields->col".
private int
qf_parse_fmt_p(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   fields->col = 0;
   for (CS match_ptr = rmp->startp[midx]; match_ptr != rmp->endp[midx]; ++match_ptr) {
      ++fields->col;
      if (*match_ptr == TAB) {
         fields->col += 7;
         fields->col -= fields->col % 8;
      }
   }
   ++fields->col;
   fields->use_viscol = true;
   return QF_OK;
}

//Parse the match for the virtual column number ('%v') pattern in regmatch.
//Return the matched value in "fields->col".
private int
qf_parse_fmt_v(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->col = (int)atol((char *)rmp->startp[midx]);
   fields->use_viscol = true;
   return QF_OK;
}

//Parse the match for the search text ('%s') pattern in regmatch.
//Return the matched value in "fields->pattern".
private int
qf_parse_fmt_s(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   int len = (int)(rmp->endp[midx] - rmp->startp[midx]);
   if (len > CMDBUFFSIZE - 5)
      len = CMDBUFFSIZE - 5;
   STRCPY(fields->pattern, "^\\V");
   STRNCAT(fields->pattern, rmp->startp[midx], len);
   fields->pattern[len + 3] = '\\';
   fields->pattern[len + 4] = '$';
   fields->pattern[len + 5] = ZERO;
   return QF_OK;
}

//Parse the match for the module ('%o') pattern in regmatch.
//Return the matched value in "fields->module".
private int
qf_parse_fmt_o(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   int len = (int)(rmp->endp[midx] - rmp->startp[midx]);
   if (len > CMDBUFFSIZE)
      len = CMDBUFFSIZE;
   STRNCAT(fields->module, rmp->startp[midx], len);
   return QF_OK;
}

//'errorformat' format pattern parser functions.
//The '%f' and '%r' formats are parsed differently from other formats.
//See parseErrorFormatMatch() for details.
//Keep in sync with FORMAT_PATTERNS[].

typedef int (*ParseFormatFn)(RegMatch *, int, Fields *);
private ParseFormatFn parseFormats[FMT_PATTERNS] = {
   NULL, //%f
   qf_parse_fmt_b,
   qf_parse_fmt_n,
   qf_parse_fmt_l,
   qf_parse_fmt_e,
   qf_parse_fmt_c,
   qf_parse_fmt_k,
   qf_parse_fmt_t,
   qf_parse_fmt_m,
   NULL, //%r
   qf_parse_fmt_p,
   qf_parse_fmt_v,
   qf_parse_fmt_s,
   qf_parse_fmt_o
};

//Parse the error format pattern matches in "regmatch" and set the values in
//"fields".  fmt_ptr contains the 'efm' format specifiers/prefixes that have a
//match.  Returns QF_OK if all the matches are successfully parsed. On
//failure, returns QF_FAIL or QF_NOMEM.
private int
parseErrorFormatMatch(
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmt_ptr,
   RegMatch* regmatch,
   Fields* fields,
   int qf_multiline,
   int qf_multiscan,
   OUT CS* tail
){
   int idx = fmt_ptr->prefix;
   int midx;
   int status;

   if ((idx == 'C' || idx == 'Z') && !qf_multiline)
      return QF_FAIL;
   fields->type = (firstOccurrence((CS)"EWIN", idx) != NULL) ? idx : 0;

   //Extract error message data from matched line.
   //We check for an actual submatch, because "\[" and "\]" in
   //the 'errorformat' may cause the wrong submatch to be used.
   for (int i = 0; i < FMT_PATTERNS; i++) {
      status = QF_OK;
      midx = (int)fmt_ptr->addr[i];
      if (i == 0 && midx > 0)            //%f
          status = qf_parse_fmt_f(regmatch, midx, fields, idx);
      ei (i == FMT_PATTERN_M) {
         if (fmt_ptr->flags == '+' && !qf_multiscan)   //%+
            status = copy_nonerror_line(linebuf, linelen, fields);
         ei (midx > 0)            //%m
            status = qf_parse_fmt_m(regmatch, midx, fields);
      } ei (i == FMT_PATTERN_R && midx > 0)   //%r
         status = qf_parse_fmt_r(regmatch, midx, tail);
      ei (midx > 0)            //others
         status = (parseFormats[i])(regmatch, midx, fields);

      if (status != QF_OK)
         return status;
   }

   return QF_OK;
}

//Parse an error line in 'linebuf' using a single error format string in
//'fmt_ptr->prog' and return the matching values in 'fields'.
//Return QF_OK if the efm format matches completely and the fields are
//successfully copied. Otherwise returns QF_FAIL or QF_NOMEM.
private int
qf_parse_get_fields(
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmt_ptr,
   Fields* fields,
   int qf_multiline,
   int qf_multiscan,
   OUT CS* tail
) {
   RegMatch   regmatch;
   int      status = QF_FAIL;

   if (qf_multiscan && firstOccurrence((CS)"OPQ", fmt_ptr->prefix) == NULL)
      return QF_FAIL;

   fields->namebuf[0] = ZERO;
   fields->bnr = 0;
   fields->module[0] = ZERO;
   fields->pattern[0] = ZERO;
   if (!qf_multiscan)
      fields->errmsg[0] = ZERO;
   fields->lnum = 0;
   fields->end_lnum = 0;
   fields->col = 0;
   fields->end_col = 0;
   fields->use_viscol = false;
   fields->enr = -1;
   fields->type = 0;
   *tail = NULL;

   //Always ignore case when looking for a matching error.
   regmatch.rm_ic = true;
   regmatch.regprog = fmt_ptr->prog;
   int r = eeRegexec(&regmatch, linebuf, (ColNr)0);
   fmt_ptr->prog = regmatch.regprog;
   if (r) {
      status = parseErrorFormatMatch(
            linebuf, linelen, fmt_ptr, &regmatch, fields, qf_multiline, qf_multiscan, tail
      );
   } 

   return status;
}

//Parse directory error format prefixes (%D and %X).
//Push and pop directories from the directory stack when scanning directory names.
private int
parse_dir_pfx(int idx, Fields* fields, LocationList *ll) {
   if (idx == 'D')   {         //enter directory
      if (*fields->namebuf == ZERO) {
         emsg(_(e_missing_or_empty_directory_name));
         return QF_FAIL;
      }
      ll->dir = pushDir(fields->namebuf, &ll->dirStack, false);
      if (ll->dir == NULL)
         return QF_FAIL;
   } ei (idx == 'X')         //leave directory
      ll->dir = popDir(&ll->dirStack);

   return QF_OK;
}

//Parse global file name error format prefixes (%O, %P and %Q).
private int
parse_file_pfx(
   int idx,
   Fields* fields,
   LocationList* ll,
   CS tail
) {
   fields->valid = false;
   if (*fields->namebuf == ZERO || mch_getperm(fields->namebuf) >= 0) {
      if (*fields->namebuf && idx == 'P')
        ll->currFName = pushDir(fields->namebuf, &ll->fileStack, true);
      ei (idx == 'Q')
        ll->currFName = popDir(&ll->fileStack);
      *fields->namebuf = ZERO;
      if (tail && *tail) {
          STRMOVE(IObuff, skipwhite(tail));
          ll->qf_multiscan = true;
          return QF_MULTISCAN;
      }
   }

   return QF_OK;
}

//Parse a non-error line (a line which doesn't match any of the error format in 'efm').
private int
qf_parse_line_nomatch(CS linebuf, int linelen, Fields* fields) {
   fields->namebuf[0] = ZERO;   //no match found, remove file name
   fields->lnum = 0;      //don't jump to this line
   fields->valid = false;

   return copy_nonerror_line(linebuf, linelen, fields);
}

//Parse multi-line error format prefixes (%C and %Z)
private int
qf_parse_multiline_pfx(
   int idx,
   LocationList* ll,
   Fields* fields
) {
   CS ptr;
   if (!ll->qf_multiignore) {
      LocLine* qfprev = ll->last;
      if (!qfprev)
          return QF_FAIL;
          
      int len;
      if (*fields->errmsg && !ll->qf_multiignore) {
         len = (int)STRLEN(qfprev->text);
         ptr = alloc_id(len + STRLEN(fields->errmsg) + 2, aid_ll_multiline_pfx);
         STRCPY(ptr, qfprev->text);
         eeglFree(qfprev->text);
         qfprev->text = ptr;
         *(ptr += len) = '\n';
         STRCPY(++ptr, fields->errmsg);
      }
      if (qfprev->errNum == -1)
          qfprev->errNum = fields->enr;
      if (bookIsCharPrintable(fields->type) && !qfprev->kind)
          //only printable chars allowed
          qfprev->kind = fields->type;

      if (!qfprev->lNum)
          qfprev->lNum = fields->lnum;
      if (!qfprev->endLNum)
          qfprev->endLNum = fields->end_lnum;
      if (!qfprev->col) {
          qfprev->col = fields->col;
          qfprev->visCol = fields->use_viscol;
      }
      if (!qfprev->endCol)
          qfprev->endCol = fields->end_col;
      if (!qfprev->fNum)
          qfprev->fNum = getBookNrForPath(ll,
             ll->dir,
             *fields->namebuf || ll->dir != NULL
             ? fields->namebuf
             : ll->currFName != NULL && fields->valid
             ? ll->currFName : 0);
   }
   if (idx == 'Z')
      ll->qf_multiline = ll->qf_multiignore = false;
   line_breakcheck();

   return QF_IGNORE_LINE;
}

//Parse a line and get the quickfix fields. Return the QF_ status.
private int
qf_parse_line(
   LocationList* ll,
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmtFirst,
   Fields* fields
){
   ErrorFormatInfo* fmt_ptr;
   int idx = 0;
   CS tail = NULL;
   int status;

restofline:
   //If there was no %> item start at the first pattern
   if (fmt_start == NULL)
      fmt_ptr = fmtFirst;
   else {
      //Otherwise start from the last used pattern
      fmt_ptr = fmt_start;
      fmt_start = NULL;
   }

   //Try to match each part of 'errorformat' until we find a complete match or no match.
   fields->valid = true;
   for ( ; fmt_ptr; fmt_ptr = fmt_ptr->next) {
      idx = fmt_ptr->prefix;
      status = qf_parse_get_fields(linebuf, linelen, fmt_ptr, fields,
               ll->qf_multiline, ll->qf_multiscan, OUT &tail);
      if (status == QF_NOMEM)
         return status;
      if (status == QF_OK)
         break;
   }
   ll->qf_multiscan = false;

   if (fmt_ptr == NULL || idx == 'D' || idx == 'X') {
      if (fmt_ptr != NULL) {
          //'D' and 'X' directory specifiers
          status = parse_dir_pfx(idx, fields, ll);
          if (status != QF_OK)
         return status;
      }

      status = qf_parse_line_nomatch(linebuf, linelen, fields);
      if (status != QF_OK)
          return status;

      if (fmt_ptr == NULL)
          ll->qf_multiline = ll->qf_multiignore = false;
   } ei (fmt_ptr != NULL) {
      //honor %> item
      if (fmt_ptr->conthere)
         fmt_start = fmt_ptr;

      if (firstOccurrence((CS)"AEWIN", idx) != NULL) {
         ll->qf_multiline = true;   //start of a multi-line message
         ll->qf_multiignore = false;//reset continuation
      } ei (firstOccurrence((CS)"CZ", idx) != NULL) {
                  //continuation of multi-line msg
         status = qf_parse_multiline_pfx(idx, ll, fields);
         if (status != QF_OK)
            return status;
      } ei (firstOccurrence((CS)"OPQ", idx) != NULL) {
                  //global file names
         status = parse_file_pfx(idx, fields, ll, tail);
         if (status == QF_MULTISCAN)
            goto restofline;
      }
      if (fmt_ptr->flags == '-') {  //generally exclude this line
         if (ll->qf_multiline)
            //also exclude continuation lines
            ll->qf_multiignore = true;
         return QF_IGNORE_LINE;
      }
   }

   return QF_OK;
}

private int
isStackEmpty(LocationStack* stack) {
   return stack == NULL || stack->listcount == 0;
}

private int
isEmpty(LocationList* ll) {
   return ll == NULL || ll->count <= 0;
}

//Returns true if the specified location list is not empty and has valid entries.
private int
listHasValidEntries(LocationList* ll) {
    return !isEmpty(ll) && !ll->noValidEntries;
}

//Return a pointer to a list in the specified quickfix stack
private LocationList *
getList(LocationStack* stack, int idx) {
    return stack->lists + idx;
}

//Allocate the fields used for parsing lines and populating a quickfix list.
private int
llAllocateFields(Fields *pfields){
   pfields->namebuf = alloc_id(CMDBUFFSIZE + 1, aid_ll_namebuf);
   pfields->module = alloc_id(CMDBUFFSIZE + 1, aid_ll_module);
   pfields->errmsglen = CMDBUFFSIZE + 1;
   pfields->errmsg = alloc_id(pfields->errmsglen, aid_ll_errmsg);
   pfields->pattern = alloc_id(CMDBUFFSIZE + 1, aid_ll_pattern);
   if (pfields->namebuf == NULL || pfields->errmsg == NULL
         || pfields->pattern == NULL || pfields->module == NULL)
      return FAIL;

   return OK;
}

//Free the fields used for parsing lines and populating a quickfix list.
private void
freeAList_fields(Fields* pfields) {
   eeglFree(pfields->namebuf);
   eeglFree(pfields->module);
   eeglFree(pfields->errmsg);
   eeglFree(pfields->pattern);
}

//Setup the state information used for parsing lines and populating a quickfix list.
private int
setupState(
   Source source,
   OUT LocationState* locState
){
   if (source.tag == SOURCE_FILENAME) {
      FILE* fd = fopen((char *)source.FileName.c, "rw");
      if (fd == NULL) {
         showErrFmtMsg(_(e_cant_open_errorfile_str), source.FileName.c);
         return FAIL;
      }
      locState->source = (Source){.tag = SOURCE_FILE, .File = (FileSource){.c = fd}};
   } 
   locState->source = source;
   return OK;
}

//Cleanup the state information used for parsing lines and populating a quickfix list.
private void
cleanupState(LocationState *locState) {
   Source source = locState->source;
   switch (source.tag) {
   case SOURCE_FILE: {
         if (source.File.c != NULL) {
            fclose(source.File.c);
         } 
      }
   default: break; 
   }
   
   eeglFree(locState->growbuf);
}

//Process the next line from a file/book/list/string and add it to the location list 'll'.
private int
processNextLine(LocationList* ll, ErrorFormatInfo* fmtFirst, LocationState* state, Fields* fields){
   //Get the next line from a file/book/list/string
   int status = getNextLine(state);
   if (status != QF_OK)
      return status;

   status = qf_parse_line(ll, state->linebuf, state->linelen, fmtFirst, fields);
   if (status != QF_OK)
      return status;

   return addEntry(ll,
      ll->dir,
      (*fields->namebuf || ll->dir != NULL)
         ? fields->namebuf : (
            (ll->currFName != NULL && fields->valid) ? ll->currFName : (CS)NULL
         ),
      fields->module,
      fields->bnr,
      fields->errmsg,
      fields->lnum,
      fields->end_lnum,
      fields->col,
      fields->end_col,
      fields->use_viscol,
      fields->pattern,
      fields->enr,
      fields->type,
      fields->user_data,
      fields->valid
   );
}

//Read the location list from a source (file, string, book or list of strings).
//Always use 'errorformat' from "buf" if there is a local value.
//Return -1 for error, number of list items for success.
private int
initWorker(
   Source source,
   OUT LocationStack* stack,
   Unt ind,
   CS errorformat,
   Boole newlist,      //true: start a new location list
   CS title
) {
   Fields fields;
   LocLine* oldLast = NULL;
   int adding = false;
   static ErrorFormatInfo* fmtFirst = NULL;
   Byte       *efm;
   static Byte   *last_efm = NULL;
   int          retval = -1;   //default: return error flag
   int          status;

   //Do not used the cached book, it may have been wiped out.
   EE_CLEAR(lastBookNameS);

   LocationState state;
   CLEAR_FIELD(state);
   CLEAR_FIELD(fields);
   if ((llAllocateFields(&fields) == FAIL)
        || (setupState(source, OUT &state) == FAIL)) {
      goto initEnd;
   } 

   LocationList* ll;
   if (newlist || ind == stack->listcount) {
      //make place for a new list
      newLocList(stack, title);
      ind = stack->currList;
      ll = getList(stack, ind);
   } else {
      //Adding to existing list, use last entry.
      adding = true;
      ll = getList(stack, ind);
      if (!isEmpty(ll))
         oldLast = ll->last;
   }

   //Use the local value of 'errorformat' if it's set.
   if (source.tag == SOURCE_BOOK && source.Book.c->o.errorFormat)
      efm = source.Book.c->o.errorFormat;
   else
      efm = errorformat;

   //If the errorformat didn't change between calls, then reuse the previously parsed values.
   if (last_efm == NULL || (STRCMP(last_efm, efm) != 0)) {
      //free the previously parsed data
      EE_CLEAR(last_efm);
      free_efm_list(&fmtFirst);

      //parse the current 'efm'
      fmtFirst = parse_efm_option(efm);
      if (fmtFirst)
         last_efm = copyStr(efm);
   }

   if (!fmtFirst)   //nothing found
      goto error2;

   //gotInterruptG is reset here, because it was probably set when killing the
   //":make" command, but we still want to read the errorfile then.
   gotInterruptG = false;

   //Read the lines in the error file one by one.
   //Try to recognize one of the error formats in each line.
   while (!gotInterruptG) {
      status = processNextLine(ll, fmtFirst, &state, &fields);
      if (status == QF_NOMEM)      //memory alloc failure
         goto initEnd;
      if (status == QF_END_OF_INPUT)   //end of input
         break;
      if (status == QF_FAIL)
         goto error2;

      line_breakcheck();
   }
   if (state.source.tag != SOURCE_FILE || !ferror(state.source.File.c)) {
      if (ll->currentIdx == 0) {
         //no valid entry found
         ll->curr = ll->first;
         ll->currentIdx = 1;
         ll->noValidEntries = true;
      } else {
         ll->noValidEntries = false;
         if (ll->curr == NULL)
            ll->curr = ll->first;
      }
      //return number of matches
      retval = ll->count;
      goto initEnd;
   }
   emsg(_(e_error_while_reading_errorfile));
   
error2:
   if (!adding) {
      //Error when creating a new list. Free the new list
      freeAList(ll);
      stack->listcount--;
      if (stack->currList > 0)
         --stack->currList;
   }
   
initEnd:
   push(*ll, stack);
   if (ind == stack->currList)
      updateBook(stack, oldLast);
   cleanupState(&state);
   freeAList_fields(&fields);

   return retval;
}

//Initialize or create a new location list in a stack and update its "change" tick
private int
initAndUpdateTick(
   Source source, OUT LocationStack* stack, NULLABLE CS errorFormat, Boole startNewList, CS title
) {
   int res = initWorker(source, stack, stack->currList, errorFormat, startNewList, title);
   if (res >= 0)
      updateChangedTick(getCurrent(stack));
   return res; 
}

//Read the errorfile "errorFName" into memory, line by line, building the error list.
//Return -1 for error, number of lines for success.
pub int
llInitFromFile(
   OUT LocationStack* st,
   CS errorFName,
   NULLABLE CS errorformat,
   Boole newlist,      //true: start a new error list
   CS title
) {
   Source source = (Source){.tag = SOURCE_FILENAME, .FileName = {.c = errorFName}};
   return initAndUpdateTick(source, OUT st, errorformat, newlist > 0, title);
}

//Set the title of the specified location list. Frees the previous title. Prepends ':' to the title
private void
storeTitle(LocationList* ll, CS title) {
   EE_CLEAR(ll->title);
   if (title == NULL)
      return;

   CS p = alloc_id(STRLEN(title) + 2, aid_ll_title);

   ll->title = p;
   if (p)
      STRCPY(p, title);
}

//The title of a location list is set, by default, to the command that created the 
//location list with the ":" prefix. Create a location list title string by prepending ":" to 
//a user command. Returns a pointer to a static buffer with the title.
private CS
copyCommandTitle(CS cmd) {
   static Byte llTitle[IOSIZE];

   eeSnprintf(llTitle, IOSIZE, ":%s", cmd);
   return llTitle;
}

//Return a pointer to the current list in the specified location stack
private LocationList *
getCurrent(LocationStack* stack) {
   return getList(stack, stack->currList);
}

//Pop a location list from the location list stack Automatically adjust currList so that it stays 
//pointed to the same list, unless it is deleted, if so then use the newest created list instead. 
//listcount will be set correctly. The above will only happen if <adjust> is true.
private void
pop(LocationStack* stack, Boole adjust) {
   freeAList(&stack->lists[0]);
   for (Unt i = 1; i < stack->listcount; ++i)
      stack->lists[i - 1] = stack->lists[i];

   //fill with zeroes now unused list at the top
   memset(stack->lists + stack->listcount - 1, 0, sizeof(*stack->lists));

   if (adjust) {
      stack->listcount--;
      if (stack->currList == 0)
         stack->currList = stack->listcount - 1;
      else
         stack->currList--;
   }
}

//Push a location list onto the top of the location stack. If it's full, free the 0th list in it
//and shift all the lists down, then add the new one on top.
private void
push(LocationList newList, LocationStack* stack) {
   if (stack->listcount + 1 < STACK_CAPACITY) {
      stack->lists[stack->listcount] = newList;
      stack->currList = stack->listcount;
      stack->listcount++;
   } else {
      LocationList* toFree = stack->lists;
      for (Unt i = 1; i < stack->listcount; ++i)
         stack->lists[i - 1] = stack->lists[i];
      freeAList(toFree);
      stack->lists[STACK_CAPACITY - 1] = newList;   
   }
}

//Prepare for adding a new location list. If the current list is in the middle of the stack, then 
//all the following lists are smashed and then the new list is added.
private void
newLocList(LocationStack* stack, CS title) {
   LocationList* ll;

   //If the current entry is not the last entry, delete entries beyond
   //the current entry. This makes it possible to browse in a tree-like way with ":grep".
   while (stack->listcount > stack->currList + 1)
      freeAList(&stack->lists[--stack->listcount]);

   //When the stack is full, remove to oldest entry Otherwise, add a new entry.
   if (stack->listcount == stack->cap) {
      pop(stack, false);
      stack->currList = stack->listcount - 1; //point to new empty list
   } else
      stack->currList = stack->listcount++;

   ll = getCurrent(stack);
   CLEAR_POINTER(ll);
   storeTitle(ll, title);
   ll->id = ++lastUsedLlIdS;
   ll->hasUserData = false;
}

//Queue location list stack delete request.
private void
locstack_queue_delreq(LocationStack* stack) {
   DeletionList* q = ALLOC_ONE(DeletionList);

   q->stack = stack;
   q->next = deletionListG;
   deletionListG = q;
}

//Return the global location stack portal buffer number.
pub int
qf_stack_get_bufnr(void) {
   if (mainStackG == NULL)
      return INVALID_LL_BUFNR;
   return mainStackG->bufNum;
}

//Wipe the location portal buffer (if present) for the specified location list.
private void
wipeLlBook(LocationStack* stack) {
   if (stack->bufNum == INVALID_LL_BUFNR)
      return;

   Book* llBook = bookFindFileByBookNr(stack->bufNum);
   if (llBook && llBook->countPortals == 0) {
      int buf_was_null = false;
      //can happen when curPor is going to be closed e.g. curPor->book
      //was already closed in closePortal(), and we are now closing the
      //portal related location list buffer from win_free_mem()
      //but bookClose() calls CHECK_CURBOOK() macro and requires curPor->book == curBook
      if (curPor->book == NULL) {
         curPor->book = curBook;
         buf_was_null = true;
      }

      //If the location buffer is not loaded in any portal, then wipe the buffer.
      bookClose(NULL, llBook, DOBOOK_WIPE, false, false);
      stack->bufNum = INVALID_LL_BUFNR;
      if (buf_was_null)
          curPor->book = NULL;
    }
}


//Free all lists in the stack (not including the stack)
private void
freeAList_list_stack_items(LocationStack* stack) {
   for (Unt i = 0; i < stack->listcount; ++i)
      freeAList(getList(stack, i));
}

//Free a LocationStack struct completely
private void
freeAList_lists(LocationStack* stack) {
   freeAList_list_stack_items(stack);

   eeglFree(stack->lists);
   eeglFree(stack);
}

//Free a location list stack
private void
ll_free_all(LocationStack** pqi) {
   LocationStack* stack = *pqi;
   if (!stack)
      return;
   *pqi = NULL;   //Remove reference to this list

   //If the location list is still in use, then queue the delete request to be processed later.
   if (qfBusynessG > 0) {
      locstack_queue_delreq(stack);
      return;
   }

   stack->refCount--;
   if (stack->refCount < 1) {
      //No references to this location list. If the location portal buffer is loaded, then wipe it
      wipeLlBook(stack);

      freeAList_lists(stack);
   }
}

//Free all the location lists in the stack.
//private void
//freeAllLocLists(int stackInd) {
//  freeAList_list_stack_items(locationStacksP + stackInd);
//}

//Delay freeing of location list stacks when the location code is running.
//Used to avoid problems with autocmds freeing location list stacks when the
//location code is still referencing the stack.
//Must always call decrementLlBusyness() exactly once after this.
private void
incrementLlBusyness(void) {
   qfBusynessG++;
}

//Safe to free location list stacks. Process any delayed delete requests.
private void
decrementLlBusyness(void) {
   if (--qfBusynessG == 0) {
      //No longer referencing the location lists. Process all the pending delete requests
      while (deletionListG != NULL) {
          DeletionList* q = deletionListG;

          deletionListG = q->next;
          ll_free_all(&q->stack);
          eeglFree(q);
      }
   }
#ifdef ABORT_ON_INTERNAL_ERROR
  if (qfBusynessG < 0) {
      emsg("qfBusynessG has become negative");
      abort();
   }
#endif
}

#if defined(EXITFREE)
pub void
check_qfBusynessG(void) {
   if (qfBusynessG != 0) {
      showErrFmtMsg("qfBusynessG not zero on exit: %ld", (long)qfBusynessG);
# ifdef ABORT_ON_INTERNAL_ERROR
      abort();
# endif
   }
}
#endif

//{{{creation

//Add an entry with source code link to the end of the list of errors.
//Return QF_OK on success or QF_FAIL on a memory allocation failure.
private int
addEntry(
   LocationList* ll,
   CS dir,      //optional directory name
   CS fname,      //file name or NULL
   CS module,   //module name or NULL
   int bufnum,      //buffer number or zero
   CS mesg,      //message
   long lnum,      //source code line number
   long end_lnum,   //source code end line number
   int col,      //column
   int end_col,   //column for end
   int vis_col,   //using visual column
   CS pattern,   //search pattern
   int nr,      //error number
   int type,      //type character
   Var* user_data,     //custom user data or NULL
   Boole valid      //valid entry
){
   Book* book;
   LocLine* lline;
   LocLine** lastp;   //pointer to last or NULL
   CS p = NULL;

   if ((lline = ALLOC_ONE_ID(LocLine, aid_ll_line)) == NULL)
      return QF_FAIL;
   if (bufnum != 0) {
      book = bookFindFileByBookNr(bufnum);

      lline->fNum = bufnum;
      if (book)
         book->hasLocationEntry = true;
   } else {
      lline->fNum = getBookNrForPath(ll, dir, fname);
      book = bookFindFileByBookNr(lline->fNum);
   }
   CS fullname = fiExpandAndCopy(fname, true);
   lline->fName = NULL;
   if (book && book->fullFileName && fullname) {
      if (fnamecmp(fullname, book->fullFileName) != 0) {
         p = shorten_fname1(fullname);
         if (p)
            lline->fName = copyStr(p);
      }
   }
   eeglFree(fullname);
   lline->text = copyStr(mesg);
   lline->lNum = lnum;
   lline->endLNum = end_lnum;
   lline->col = col;
   lline->endCol = end_col;
   lline->visCol = vis_col;
   if (user_data == NULL || user_data->tag == VAR_UNKNOWN)
      lline->userData.tag = VAR_UNKNOWN;
   else {
      copy_tv(OUT &lline->userData, user_data);
      ll->hasUserData = true;
   }
   if (!pattern || *pattern == ZERO)
      lline->pattern = NULL;
   else 
      lline->pattern = copyStr(pattern);
      
   if (!module || *module == ZERO)
      lline->moduleName = NULL;
   else 
      lline->moduleName = copyStr(module);
   lline->errNum = nr;
   if (type != 1 && !bookIsCharPrintable(type)) //only printable chars allowed
      type = 0;
   lline->kind = type;
   lline->isValid = valid;

   lastp = &ll->last;
   if (isEmpty(ll)) {     //first element in the list
      ll->first = lline;
      ll->curr = lline;
      ll->currentIdx = 0;
      lline->prev = NULL;
   } else {
      lline->prev = *lastp;
      (*lastp)->next = lline;
   }
   lline->next = NULL;
   lline->isCleared = false;
   *lastp = lline;
   ++ll->count;
   if (ll->currentIdx == 0 && lline->isValid) {  //first valid entry
      ll->currentIdx = ll->count;
      ll->curr = lline;
   }

   return QF_OK;
}

//Allocate memory for lists member of LocationStack struct.
private LocationList *
allocateLocList(int n) {
   return ALLOC_CLEAR_MULT(LocationList, n);
}


//Initialize all location stacks. Should only be called once.
pub void
llInitStacksOnce(void) {
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      LocationStack* st = locationStacksP + i;
      st->bufNum = INVALID_LL_BUFNR;
      st->lists = allocateLocList(STACK_CAPACITY);
      st->listcount = 0;
   }
}

//}}}
//{{{identification

private LocationStack*
identifyStackByLetter(char letter) {
   switch (letter) {
   case 'm': return locationStacksP + LOC_LIST_MAKE;
   case 'g': return locationStacksP + LOC_LIST_GREP;
   case 'h': return locationStacksP + LOC_LIST_HELP;
   case 't': return locationStacksP + LOC_LIST_TAGS;
   case 'b': return locationStacksP + LOC_LIST_BOOKMARKS;
   case 'c': return locationStacksP + LOC_LIST_CSCOPE;
   default: return NULL;
   }
}

//In commands, loc stacks are identified by a letter like `:lopen g`
//Decode the letter from arg and return one of LOC_LIST_* constants or -1 in case of incorrect arg
private LocationStack*
identifyStack(Var* arg) {
   if (arg->tag != VAR_STRING)
      return NULL;
   return identifyStackByLetter(*(arg->string));
}

private LocationStack*
identifyStackByInvo(Invocation* invo) {
   return identifyStackByLetter(*(invo->arg));
}

//Get the location list stack to use for the specified Command.
private LocationStack *
getStackForCommand(Invocation* invo, int print_emsg) {
   LocationStack* stack = identifyStackByInvo(invo);
   if (stack == NULL && print_emsg)
      emsg(_(e_no_location_stack));

   return stack;
}

//}}}

//Copy location list entries from 'source' to 'dest'.
//private int
//copy_loclist_entries(LocationList* source, LocationList* dest) {
//  int i;
//  LocLine* from_qfp;
//  LocLine* prevp;
//
//  // copy all the location entries in this list
//  FOR_ALL_LL_ITEMS(source, from_qfp, i) {
//     if (addEntry(dest,
//            NULL,
//            NULL,
//            from_qfp->moduleName,
//            0,
//            from_qfp->text,
//            from_qfp->lNum,
//            from_qfp->endLNum,
//            from_qfp->col,
//            from_qfp->endCol,
//            from_qfp->visCol,
//            from_qfp->pattern,
//            from_qfp->errNum,
//            0,
//            &from_qfp->userData,
//            from_qfp->isValid) == QF_FAIL)
//        return FAIL;
//
//     // addEntry() will not set the qf_num field, as the
//     // directory and file names are not supplied. So the fNum
//     // field is copied here.
//     prevp = dest->last;
//     prevp->fNum = from_qfp->fNum;   // file number
//     prevp->kind = from_qfp->kind;   // error type
//     if (source->curr == from_qfp)
//        dest->curr = prevp;      // current location
//  }
//
//  return OK;
//}

//Copy the specified location list 'source' to 'dest'.
//private int
//copy_loclist(LocationList *source, LocationList *dest) {
//  // Some of the fields are populated by addEntry()
//  dest->noValidEntries = source->noValidEntries;
//  dest->hasUserData = source->hasUserData;
//  dest->count = 0;
//  dest->currentIdx = 0;
//  dest->first = NULL;
//  dest->last = NULL;
//  dest->curr = NULL;
//  if (source->title != NULL)
//     dest->title = copyStr(source->title);
//  else
//     dest->title = NULL;
//  if (source->qf_ctx) {
//     dest->qf_ctx = allocVar();
//     if (dest->qf_ctx)
//        copy_tv(OUT dest->qf_ctx, source->qf_ctx);
//  } else
//     dest->qf_ctx = NULL;
//  if (source->textFn.name != NULL)
//     evCopyCallback(&dest->textFn, &source->textFn);
//  else
//     dest->textFn.name = NULL;
//
//  if (source->count) {
//     if (copy_loclist_entries(source, dest) == FAIL)
//        return FAIL;
//  } 
//
//  dest->currentIdx = source->currentIdx;   // current index in the list
//
//  // Assign a new ID for the location list
//  dest->id = ++lastUsedLlIdS;
//  dest->changedTick = 0L;
//
//  // When no valid entries are present in the list, curr points to
//  // the first item in the list
//  if (dest->noValidEntries) {
//     dest->curr = dest->first;
//     dest->currentIdx = 1;
//  }
//
//  return OK;
//}

//Get book number for file "directory/fname". Also sets the hasLocationEntry flag.
private int
getBookNrForPath(LocationList* ll, CS directory, CS fname) {
   CS ptr = NULL;
   Book* book;
   CS bufname;

   if (fname == NULL || *fname == ZERO)      //no file name
      return 0;

   if (directory && !eeIsAbsName(fname)
       && (ptr = concat_fnames(directory, fname, true)) != NULL
   ) {
      //Here we check if the file really exists.
      //This should normally be true, but if make works without
      //"leaving directory"-messages we might have missed a directory change.
      if (mch_getperm(ptr) < 0) {
         eeglFree(ptr);
         directory = guessFilepath(ll, fname);
         if (directory)
            ptr = concat_fnames(directory, fname, true);
         else
            ptr = copyStr(fname);
      }
      //Use concatenated directory name and file name
      bufname = ptr;
   } else
      bufname = fname;

   if (lastBookNameS && STRCMP(bufname, lastBookNameS) == 0 && bookRefValid(&last_bufref)) {
      book = last_bufref.c;
      eeglFree(ptr);
   } else {
      eeglFree(lastBookNameS);
      book = bookNew(bufname, NULL, (LineNr)0, BLN_NOOPT);
      if (bufname == ptr)
         lastBookNameS = bufname;
      else
         lastBookNameS = copyStr(bufname);
      bookStoreInRef(OUT &last_bufref, book);
   }
   if (!book)
      return 0;

   book->hasLocationEntry = true;
   return book->fiNum;
}

//Push dirbuf onto the directory stack and return pointer to actual dir or NULL on error.
private CS
pushDir(CS dirbuf, DirStack** stackptr, int is_file_stack) {
   DirStack* ds_ptr;

   //allocate new stack element and hook it in
   DirStack* ds_new = ALLOC_ONE_ID(DirStack, aid_ll_dirstack);
   if (ds_new == NULL)
      return NULL;

   ds_new->next = *stackptr;
   *stackptr = ds_new;

   //store directory on the stack
   if (eeIsAbsName(dirbuf)
          || (*stackptr)->next == NULL
          || is_file_stack)
      (*stackptr)->dirname = copyStr(dirbuf);
   else {
      //Okay we don't have an absolute path.
      //dirbuf must be a subdir of one of the directories on the stack.
      //Let's search...
      ds_new = (*stackptr)->next;
      (*stackptr)->dirname = NULL;
      while (ds_new) {
         eeglFree((*stackptr)->dirname);
         (*stackptr)->dirname = concat_fnames(ds_new->dirname, dirbuf, true);
         if (mch_isdir((*stackptr)->dirname) == true)
            break;

         ds_new = ds_new->next;
      }

      //clean up all dirs we already left
      while ((*stackptr)->next != ds_new) {
         ds_ptr = (*stackptr)->next;
         (*stackptr)->next = (*stackptr)->next->next;
         eeglFree(ds_ptr->dirname);
         eeglFree(ds_ptr);
      }

      //Nothing found -> it must be on top level
      if (ds_new == NULL) {
         eeglFree((*stackptr)->dirname);
         (*stackptr)->dirname = copyStr(dirbuf);
      }
   }

   if ((*stackptr)->dirname != NULL)
      return (*stackptr)->dirname;
   else {
      ds_ptr = *stackptr;
      *stackptr = (*stackptr)->next;
      eeglFree(ds_ptr);
      return NULL;
   }
}

//pop dirbuf from the directory stack and return previous directory or NULL if stack is empty
private CS
popDir(DirStack **stackptr) {
   DirStack  *ds_ptr;

   //TODO: Should we check if dirbuf is the directory on top of the stack?
   //What to do if it isn't?

   //pop top element and free it
   if (*stackptr != NULL) {
      ds_ptr = *stackptr;
      *stackptr = (*stackptr)->next;
      eeglFree(ds_ptr->dirname);
      eeglFree(ds_ptr);
   }

   //return NEW top element as current dir or NULL if stack is empty
   return *stackptr ? (*stackptr)->dirname : NULL;
}

//clean up directory stack
private void
qf_clean_dir_stack(DirStack** stackptr) {
   DirStack* ds_ptr;

   while ((ds_ptr = *stackptr) != NULL) {
      *stackptr = (*stackptr)->next;
      eeglFree(ds_ptr->dirname);
      eeglFree(ds_ptr);
   }
}

//Check in which directory of the directory stack the given file can be found.
//Returns a pointer to the directory name or NULL if not found.
//Clean up intermediate directory entries.
//
//TODO: How to solve the following problem?
//If we have this directory tree:
//   ./
//   ./aa
//   ./aa/bb
//   ./bb
//   ./bb/x.c
//and make says:
//   making all in aa
//   making all in bb
//   x.c:9: Error
//Then pushDir thinks we are in ./aa/bb, but we are in ./bb.
//guessFilepath will return NULL.
private CS
guessFilepath(LocationList* ll, CS filename) {
   //no dirs on the stack - there's nothing we can do
   if (ll->dirStack == NULL)
      return NULL;

   DirStack* ds_ptr = ll->dirStack->next;
   CS fullname = NULL;
   while (ds_ptr) {
      eeglFree(fullname);
      fullname = concat_fnames(ds_ptr->dirname, filename, true);

      //If concat_fnames failed, just go on. The worst thing that can happen
      //is that we delete the entire stack.
      if ((fullname != NULL) && (mch_getperm(fullname) >= 0))
         break;

      ds_ptr = ds_ptr->next;
   }

   eeglFree(fullname);

   //clean up all dirs we already left
   DirStack* ds_tmp;
   while (ll->dirStack->next != ds_ptr) {
      ds_tmp = ll->dirStack->next;
      ll->dirStack->next = ll->dirStack->next->next;
      eeglFree(ds_tmp->dirname);
      eeglFree(ds_tmp);
   }

   return ds_ptr == NULL ? NULL : ds_ptr->dirname;
}

//Return true if a location list with the given identifier exists.
private int
isIdValid(LocationStack* st, Unt id){
   if (!st)
      return false;

   for (Unt i = 0; i < st->listcount; ++i) {
      if (st->lists[i].id == id)
         return true;
   } 

   return false;
}

//When loading a file from the location list, the autocommands may modify it.
//This may invalidate the current entry.  This function checks
//whether an entry is still present in the list. Similar to location list.
private int
isEntryPresent(LocationList *ll, LocLine *curr) {
   LocLine* lline;
   int i;
   //Search for the entry in the current list
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if (lline == curr)
         break;
   } 

   if (i > ll->count) //Entry is not found
      return false;

   return true;
}

//Get the next valid entry in the current location list. Start search from the current entry. 
//Return NULL on failure.
private LocLine *
getNextValidEntry(LocationList* ll, LocLine* curr, int* currentIdx, Unt dir) {
   int idx = *currentIdx;
   int old_fNum = curr->fNum;

   do {
      if (idx == ll->count || curr->next == NULL)
         return NULL;
      ++idx;
      curr = curr->next;
   } while ((!ll->noValidEntries && !curr->isValid)
       || (dir == FORWARD_FILE && curr->fNum == old_fNum));

   *currentIdx = idx;
   return curr;
}

//Get the previous valid entry in the current location list. The
//search starts from the current entry.  Returns NULL on failure.
private LocLine *
getPrevValidEntry(LocationList* ll, LocLine* curr, int* currentIdx, Unt dir) {
   int idx = *currentIdx;
   int old_fNum = curr->fNum;

   do {
      if (idx == 1 || curr->prev == NULL)
         return NULL;
      --idx;
      curr = curr->prev;
    } while ((!ll->noValidEntries && !curr->isValid)
       || (dir == BACKWARD_FILE && curr->fNum == old_fNum));

    *currentIdx = idx;
    return curr;
}

//Get the n'th (errornr) previous/next valid entry from the current entry in the location list.
// dir == FORWARD or FORWARD_FILE: next valid entry
// dir == BACKWARD or BACKWARD_FILE: previous valid entry
private LocLine *
get_nth_valid_entry(LocationList* ll, int errornr, Unt dir, int* new_qfidx){
   LocLine* curr = ll->curr;
   int ind = ll->currentIdx;
   LocLine* prev_curr;
   int prev_index;
   CS err = e_no_more_items;

   while (errornr--) {
      prev_curr = curr;
      prev_index = ind;

      if (dir == FORWARD || dir == FORWARD_FILE)
         curr = getNextValidEntry(ll, curr, &ind, dir);
      else
         curr = getPrevValidEntry(ll, curr, &ind, dir);
      if (curr == NULL) {
         curr = prev_curr;
         ind = prev_index;
         if (err) {
            return NULL;
         }
         break;
      }

      err = NULL;
   }

   *new_qfidx = ind;
   return curr;
}

//Get n'th (errornr) entry from the current entry in the location
//list 'll'. Return a pointer to the new entry and the index in 'new_qfidx'
private LocLine *
getNthEntry(LocationList* ll, int errornr, int* new_qfidx) {
   LocLine* curr = ll->curr;
   int ind = ll->currentIdx;

   //New error number is less than the current error number
   while (errornr < ind && ind > 1 && curr->prev) {
      --ind;
      curr = curr->prev;
   }
   //New error number is greater than the current error number
   while (errornr > ind && ind < ll->count && curr->next) {
      ++ind;
      curr = curr->next;
   }

   *new_qfidx = ind;
   return curr;
}

//Get an entry specified by 'errornr' and 'dir' from the current location list. 'errornr' 
//specifies the index of the entry and 'dir' specifies the direction 
//(FORWARD/BACKWARD/FORWARD_FILE/BACKWARD_FILE).
//Return a pointer to the entry and the index the new entry is stored at, 'new_qfidx'.
private LocLine *
getEntry(LocationList* ll, int errornr, Unt dir, OUT int* new_qfidx){
   LocLine   *curr = ll->curr;
   int      qfidx = ll->currentIdx;

   if (dir != 0)    //next/prev valid entry
      curr = get_nth_valid_entry(ll, errornr, dir, &qfidx);
   ei (errornr != 0)   //go to specified number
      curr = getNthEntry(ll, errornr, &qfidx);

   *new_qfidx = qfidx;
   return curr;
}

//Find a portal displaying a Vim help file in the current tab.
private Portal *
findHelpPortal(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (bookIsHelp(po->book))
          return po;
   } 

   return NULL;
}

//Find a help portal or open one. If 'newPort' is true, then open a new help portal.
private int
jumpToHelpPortal(int newPort, int *openedPortal) {
   int      flags;

   Portal* helpPort;
   if (commModifierG.cmod_tab != 0 || newPort)
      helpPort = NULL;
   else
      helpPort = findHelpPortal();
   if (helpPort && helpPort->book->countPortals > 0)
      enterPortal(helpPort, true);
   else {
      //Split off help portal; put it at far top if no position
      //specified, the current portal is vertically split and narrow.
      flags = WSP_HELP;
      if (commModifierG.cmod_split == 0 && curPor->width != visibleColsG && curPor->width < 80)
         flags |= WSP_TOP;
      //If the user asks to open a new portal, then copy the location list.
      //Otherwise, don't copy the location list.
      if (!newPort)
         flags |= WSP_NEWLOC;

      if (splitPortal(0, flags) == FAIL)
         return FAIL;

      *openedPortal = true;

      if (curPor->height < p_hh)
         portSetHeight((int)p_hh, curPor);
   }
   restart_edit = 0;

   return OK;
}

//Find a portal into a normal book in the current tab.
private Portal *
findPortalIntoLocList_with_normal_buf(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (bt_normal(po->book))
         return po;
   } 

   return NULL;
}

//Go to a portal in any tab containing the specified file.  Returns true
//if successfully jumped to the portal. Otherwise returns false.
private int
qf_goto_tabwin_with_file(int fnum) {
   Tab* t;
   Portal* po;

   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book->fiNum == fnum) {
          goto_tab_port(t, po);
          return true;
      }
   } 

   return false;
}

//Create a new portal to show a file above the location portal. Called when
//only the location portal is present.
private int
open_new_file_port(LocationStack* llRef) {
   int flags = WSP_ABOVE;
   if (llRef)
      flags |= WSP_NEWLOC;
   if (splitPortal(0, flags) == FAIL)
      return FAIL;      //not enough room for portal
   p_swb = 0;   //don't split again
   curPor->o.diff = false;
   return OK;
}

//Go to a portal that shows the right book. If the portal is not found, go
//to the portal just above the location portal. This is used for opening
//a file from a location portal and not from a location portal. If some usable
//portal is previously found, then it is supplied in 'use_win'.
private void
gotoPortalIntoLlFile(Portal* usePort, int fNum) {
   Portal* port = usePort;
   if (!port) {
      //Find the window showing the selected file in the current tab.
      FOR_ALL_PORTALS(port) {
         if (port->book->fiNum == fNum)
            break;
      } 
      if (!port) {
         //Find a previous usable window
         port = curPor;
         do {
            if (bt_normal(port->book))
               break;
            if (port->prev == NULL)
               port = lastPor;   //wrap around the top
            else
               port = port->prev; //go to previous window
         } while (port != curPor);
      }
  }
  gotoPortal(port);
}

//Go to a portal that contains the specified book 'fNum'. If a portal is
//not found, then go to the portal just above the location portal. This is
//used for opening a file from a location portal and not from a location
//portal.
private void
gotoPortalIntoQflFile(int fNum) {
   Portal* port = curPor;
   Portal* altPort = NULL;
   for (;;) {
      if (port->book->fiNum == fNum)
         break;
      if (!port->prev)
         port = lastPor;   //wrap around the top
      else
         port = port->prev;   //go to previous window

      if (isLocListPortalDOW(port)) {
         //Didn't find it, go to the portal before the location portal, unless 'switchbook' 
         //contains 'uselast': in this case we try to jump to the previously used window first.
         if ((p_swb & SWB_USELAST) != 0 && portalIsValid(prevPor) && !prevPor->o.portFixBuf)
            port = prevPor;
         ei (altPort)
            port = altPort;
         ei (curPor->prev)
            port = curPor->prev;
         else
            port = curPor->next;
         break;
      }

      //Remember a usable portal
      if (!altPort && !port->isPreview && !port->o.portFixBuf && bt_normal(port->book))
         altPort = port;
   }

   gotoPortal(port);
}

//Find a suitable portal for opening a file (fNum) from the location list and jump to it.  If 
//there already is a portal into the file, jump to it. Otherwise open a new portal into the file.
//If 'newPort' is true, then always open a new portal. This is called from  location list portals.
private int
jumpToUsablePortal(int fNum, int newPort, int* openedPortal) {
   Portal* usable_wp = NULL;
   int usablePort = false;

   //If opening a new portal, then don't use the location list referred by
   //the current portal. Otherwise two windows will refer to the same location list.
   LocationStack* llRef = newPort ? null : curPor->locationStackRef;

   if (llRef) {
      //Find a non-LL portal with this location list
      usable_wp = NULL;
      if (usable_wp)
         usablePort = true;
   }

   if (!usablePort) {
      //Locate a portal showing a normal book
      Portal* port = findPortalIntoLocList_with_normal_buf();
      if (port)
         usablePort = true;
   }

   //If no usable portal is found and 'switchbook' contains "usetab" then search in other tabs
   if (!usablePort && (p_swb & SWB_USETAB) != 0)
      usablePort = qf_goto_tabwin_with_file(fNum);

   //If there is only one portal and it is a location portal, create a new one above it
   if ((ONLY_ONE_PORTAL && isLocationListBook(curBook)) || !usablePort || newPort) {
      if (open_new_file_port(llRef) != OK)
         return FAIL;
      *openedPortal = true;   //close it when fail
   } else {
      if (curPor->locationStackRef != NULL)   //In a location portal
         gotoPortalIntoLlFile(usable_wp, fNum);
      else               //In a location portal
         gotoPortalIntoQflFile(fNum);
   }

   return OK;
}

//Edit the selected file or help file. Returns OK if successfully edited the file, FAIL on failing
//to open the book and QF_ABORT if the location list was freed by an autocmd when opening the 
//book.
private int
jumpAndEditBook(
   LocationStack* stack,
   LocLine* curr,
   int forceit,
   int prevPortId,
   int* openedPortal
){
   LocationList* ll = getCurrent(stack);
   int old_changedtick = ll->changedTick;
   int retval = OK;
   Unt old_currList = stack->currList;
   int idSave = ll->id;

   if (curr->kind == 1) {
      //Open help file (startEditingFile() will set kind == BOOK_HELP, readfile() will
      //set readonly flag).
      retval = startEditingFile(curr->fNum, NULL, NULL, NULL, (LineNr)1,
         ECMD_HIDE + ECMD_SET_HELP, prevPortId == curPor->id ? curPor : NULL
      );
   } else {
      int   fnum = curr->fNum;

      if (!forceit && curPor->o.portFixBuf && curBook->fiNum != fnum) {
         if (curPor->locationStackRef != NULL) { 
            //Location lists cannot split or reassign their portal so 'portfixbuf' portals must fail
            emsg(_(e_portfixbuf_cannot_go_to_buffer));
            return FAIL;
         } 

         if (portalIsValid(prevPor) && !prevPor->o.portFixBuf 
               && !isLocationListBook(prevPor->book)
         ) {
            //'portfixbuf' is set; attempt to change to a window without it
            //that isn't a location list portal.
            gotoPortal(prevPor);
         }
         if (curPor->o.portFixBuf) {
            //Split the window, which will be 'noportfixbuf', and set curPor to that
            if (splitPortal(0, 0) == OK)
               *openedPortal = true;

            if (curPor->o.portFixBuf) {
               //Autocommands set 'portfixbuf' or sent us to another window
               //with it set, or we failed to split the window. Give up,
               //but don't return immediately, as they may have messed with the list.
               emsg(_(e_portfixbuf_cannot_go_to_buffer));
               retval = FAIL;
            }
         }
      }

      if (retval == OK) {
         retval = booklistGetFile(fnum, (LineNr)1, GETF_SETMARK | GETF_SWITCH, forceit);
      }
   }

   //If a location list, check whether the associated portal is still present
   Portal* wp = getPortalById(prevPortId);
   if (!wp) {
      emsg(_(e_current_window_was_closed));
      *openedPortal = false;
      return QF_ABORT;
   }

   if (!isIdValid(stack, idSave)) {
      emsg(_(e_current_location_list_was_changed));
      return QF_ABORT;
   }

   //Check if the list was changed. Pointers may happen to be identical, so also check changedTick
   if (old_currList != stack->currList
       || old_changedtick != ll->changedTick
       || !isEntryPresent(ll, curr)
   ) {
      emsg(_(e_current_location_list_was_changed));
      return QF_ABORT;
   }

   return retval;
}

//Go to an entry in the current file using either line/column number or a search pattern.
private void
jumpToEntry(LineNr lNum, int col, Byte visCol, CS pattern){
   LineNr i;

   if (!pattern) {
      //Go to line with error, unless lNum is 0.
      i = lNum;
      if (i > 0) {
          if (i > curBook->mem.lineCount)
         i = curBook->mem.lineCount;
          curPor->cursor.lnum = i;
      }
      if (col > 0) {
         curPor->cursor.coladd = 0;
         if (visCol == true)
            coladvance(col - 1);
         else
            curPor->cursor.col = col - 1;
         curPor->setCursWant = true;
         check_cursor();
      } else
         beginline(BL_WHITE | BL_FIX);
   } else {
      Pos save_cursor;

      //Move the cursor to the first line in the book
      save_cursor = curPor->cursor;
      curPor->cursor.lnum = 0;
      if (!do_search(NULL, '/', '/', text(pattern), (long)1, SEARCH_KEEP, NULL))
         curPor->cursor = save_cursor;
   }
}

//Display location list index and size message
private void
printMsg(
   LocationStack* stack,
   int currentIdx,
   LocLine* curr,
   Book* oldCurBook,
   LineNr old_lnum
) {
   ArrayList* gap = getTempList();

   //Update the screen before showing the message, unless the screen scrolled up.
   if (!msg_scrolled)
      update_topline_redraw();
   eeSnprintf(IObuff, IOSIZE, _("(%d of %d)%s%s: "), currentIdx,
       getCurrent(stack)->count,
       curr->isCleared ? _(" (line deleted)") : S"",
       createMsg(curr->kind, curr->errNum));
   //Add the message, skipping leading whitespace and newlines.
   ga_concat(gap, IObuff);
   formatText(gap, skipwhite(curr->text));
   ga_append(gap, ZERO);

   //Output the message. Overwrite to avoid scrolling when the 'O'
   //flag is present in 'shortmess'; But when not jumping, print the whole message.
   LineNr i = msg_scroll;
   if (curBook == oldCurBook && curPor->cursor.lnum == old_lnum)
      msg_scroll = true;
   ei (!msg_scrolled)
      msg_scroll = false;
   msgAndKeep((CS)gap->c, 0, true);
   msg_scroll = i;

   clearArrayList();
}

//Find a usable portal for opening a file from the location list. If a portal is not found then 
//open a new portal. If 'newPort' is true, then open a new portal. Return OK if successfully 
//jumped or opened a portal. Return FAIL if not able to jump/open a portal. Return NOTDONE if 
//a file is not associated with the entry. Return QF_ABORT if the location list was modified
//by an autocmd.
private int
jumpOrOpenPortal(LocationStack* stack, LocLine* curr, int newPort, int* openedPortal){
   LocationList* ll = getCurrent(stack);
   int old_changedtick = ll->changedTick;
   Unt old_currList = stack->currList;

   //For ":helpgrep" find a help portal or open one.
   if (curr->kind == 1 && (!bookIsHelp(curPor->book) || commModifierG.cmod_tab != 0)
       && jumpToHelpPortal(newPort, openedPortal) == FAIL)
          return FAIL;
          
   if (old_currList != stack->currList
       || old_changedtick != ll->changedTick
       || !isEntryPresent(ll, curr)
   ){
      emsg(_(e_current_location_list_was_changed));
      return QF_ABORT;
   }

   //If currently in the location portal, find another portal to show the file in.
   if (isLocationListBook(curBook) && !*openedPortal) {
      //If there is no file specified, we don't know where to go.
      //But do advance, otherwise ":mn" gets stuck.
      if (curr->fNum == 0)
          return NOTDONE;
      if (jumpToUsablePortal(curr->fNum, newPort, openedPortal) == FAIL)
          return FAIL;
   }
   if (old_currList != stack->currList
       || old_changedtick != ll->changedTick
       || !isEntryPresent(ll, curr)
   ) {
      emsg(_(e_current_location_list_was_changed));
   }

   return OK;
}

//Edit a selected file from the location list and jump to a particular line/column, adjust the 
//folds and display a message about the jump. Returns OK on success and FAIL on failing to open 
//the file/book. Return QF_ABORT if the location list is freed by an autocmd when opening the file.
private int
jumpToBook(
   LocationStack* stack,
   int currentIdx,
   LocLine* curr,
   int forceit,
   int prevPortId,
   int* openedPortal,
   int openfold,
   int print_message
) {
   int retval = OK;

   //If there is a file name, read the wanted file if needed, and check autowrite etc.
   Book* oldCurBook = curBook;
   LineNr old_lnum = curPor->cursor.lnum;

   if (curr->fNum != 0) {
      retval = jumpAndEditBook(stack, curr, forceit, prevPortId, openedPortal);
      if (retval != OK)
          return retval;
   }

   //When not switched to another book, still need to set pc mark
   if (curBook == oldCurBook)
      setpcmark();

   jumpToEntry(curr->lNum, curr->col, curr->visCol, curr->pattern);

   if ((p_fdo & FDO_LOCATION) != 0 && openfold)
      foldOpenCursor();
   if (print_message)
      printMsg(stack, currentIdx, curr, oldCurBook, old_lnum);

   return retval;
}

pub LocationStack*
getLocationStack(int ind) {
   if (ind < 0 || ind >= COUNT_LOC_LISTS)
      return NULL;
   return locationStacksP + ind;   
}

//Jump to an entry and try to use an existing portal.
pub void
llJump(LocationStack* stack, Unt dir, int errornr, Boole forceit){
   jumpToNewPortal(stack, dir, errornr, forceit, false);
}

//Jump to a loclist line.
//If dir == 0 go to entry "errornr".
//If dir == FORWARD go "errornr" valid entries forward.
//If dir == BACKWARD go "errornr" valid entries backward.
//If dir == FORWARD_FILE go "errornr" valid entries files backward.
//If dir == BACKWARD_FILE go "errornr" valid entries files backward
//ei "errornr" is zero, redisplay the same line
//If 'forceit' is true, then can discard changes to the current buffer.
//If 'newPort' is true, then open the file in a new portal.
private void
jumpToNewPortal(
   LocationStack* stack,
   Unt dir,
   int errornr,
   Boole forceit,
   Boole newPort
){
   LocationList* ll;
   LocLine* curr;
   LocLine* old_curr;
   int currentIdx;
   int old_currentIdx;
   Unt old_swb = p_swb;
   int prevPortId;
   int openedPortal = false;
   int print_message = true;
   int old_keyWasTypedG = keyWasTypedG; //getting file may reset it
   int retval = OK;

   if (isStackEmpty(stack) || isEmpty(getCurrent(stack))) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   incrementLlBusyness();

   ll = getCurrent(stack);

   curr = ll->curr;
   old_curr = curr;
   currentIdx = ll->currentIdx;
   old_currentIdx = currentIdx;

   curr = getEntry(ll, errornr, dir, &currentIdx);
   if (curr == NULL) {
      curr = old_curr;
      currentIdx = old_currentIdx;
      goto theend;
   }

   ll->currentIdx = currentIdx;
   ll->curr = curr;
   if (updatePortalPos(stack, old_currentIdx))
      //No need to print the error message if it's visible in the error portal
      print_message = false;

   prevPortId = curPor->id;

   retval = jumpOrOpenPortal(stack, curr, newPort, &openedPortal);
   if (retval == FAIL)
      goto failed;
   if (retval == QF_ABORT) {
      stack = NULL;
      curr = NULL;
      goto theend;
   }
   if (retval == NOTDONE)
      goto theend;

   retval = jumpToBook(stack, currentIdx, curr, forceit, prevPortId,
              &openedPortal, old_keyWasTypedG, print_message);
   if (retval == QF_ABORT) {
      //Location list was modified by an autocomm
      stack = NULL;
      curr = NULL;
   }

   if (retval != OK) {
      if (openedPortal)
         closePortal(curPor, true);    //Close opened portal
      if (curr != NULL && curr->fNum != 0) {
         //Couldn't open file, so put index back where it was.  This could
         //happen if the file was readonly and we changed something.
   failed:
         curr = old_curr;
         currentIdx = old_currentIdx;
      }
   }
theend:
   if (stack) {
      ll->curr = curr;
      ll->currentIdx = currentIdx;
   }
   if (p_swb != old_swb) {
      //Restore old @switchbook value, but not when an autocommand has changed the value.
      p_swb = old_swb;
   }
   decrementLlBusyness();
}

//Highlight attributes used for displaying entries from the location list.
private Decoration fileDeco;
private Decoration separatorDeco;
private Decoration lineDeco;

//Display information about a single entry from the location list.
//Used by ":mlist/:llist" commands.
//'cursel' will be set to true for the currently selected entry in the list.
private void
displayListEntry(LocLine* lline, int ind, int cursel) {
   Book* book;
   int filter_entry;
   ArrayList* gap;

   CS fname = NULL;
   if (lline->moduleName != NULL && *lline->moduleName != ZERO)
      eeSnprintf(IObuff, IOSIZE, "%2d %s", ind, lline->moduleName);
   else {
      if (lline->fNum != 0 && (book = bookFindFileByBookNr(lline->fNum)) != NULL) {
         if (lline->fName == NULL)
            fname = book->currFileName;
         else
            fname = lline->fName;
         if (lline->kind == 1)   //:helpgrep
            fname = fiGetShortFiName(fname);
      }
      if (fname == NULL)
         sprintf((char *)IObuff, "%2d", ind);
      else
         eeSnprintf(IObuff, IOSIZE, "%2d %s", ind, fname);
   }

   //Support for filtering entries using :filter /pat/ clist Match against the module name, file 
   //name, search pattern and text of the entry.
   filter_entry = true;
   if (lline->moduleName != NULL && *lline->moduleName != ZERO)
      filter_entry &= message_filtered(lline->moduleName);
   if (filter_entry && fname != NULL)
      filter_entry &= message_filtered(fname);
   if (filter_entry && lline->pattern != NULL)
      filter_entry &= message_filtered(lline->pattern);
   if (filter_entry)
      filter_entry &= message_filtered(lline->text);
   if (filter_entry)
      return;

   msg_putchar('\n');
   msgOuttransDeco(IObuff, cursel ? getDecoFlags(HLF_QFL) : fileDeco.flags);

   if (lline->lNum != 0)
      msgPutsDeco(S":", separatorDeco.flags);
      
   gap = getTempList();
   if (lline->lNum != 0)
      addRangeInformationToArrayList(gap, lline);
      
   ga_concat(gap, createMsg(lline->kind, lline->errNum));
   ga_append(gap, ZERO);
   msgPutsDeco((CS)gap->c, lineDeco.flags);
   msgPutsDeco(S":", separatorDeco.flags);
   if (lline->pattern != NULL) {
      gap = getTempList();
      formatText(gap, lline->pattern);
      ga_append(gap, ZERO);
      msg_puts(gap->c);
      msgPutsDeco(S":", separatorDeco.flags);
   }
   msg_puts(S" ");

   //Remove newlines and leading whitespace from the text.  For an
   //unrecognized line keep the indent, the compiler may mark a word
   //with ^^^^.
   gap = getTempList();
   formatText(gap, (fname != NULL || lline->lNum != 0) ? skipwhite(lline->text) : lline->text);
   ga_append(gap, ZERO);
   msg_prt_line((CS)gap->c, false);
   out_flush();      //show one line at a time
}

//":llist": list all locations
pub void
c_list(Invocation* invo) {
   int i;
   int idx1 = 1;
   int idx2 = -1;
   CS arg = invo->arg;
   Boole plus = false;
   int all = invo->forceit;   //if not :ml!, only show recognized errors
   LocationStack   *stack;
   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   if (isStackEmpty(stack) || isEmpty(getCurrent(stack))) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }
   if (*arg == '+') {
      ++arg;
      plus = true;
   }
   if (!get_list_range(&arg, &idx1, &idx2) || *arg != ZERO) {
      showErrFmtMsg(_(e_trailing_characters_str), arg);
      return;
   }
   LocationList* ll = getCurrent(stack);
   if (plus) {
      i = ll->currentIdx;
      idx2 = i + idx1;
      idx1 = i;
   } else {
      i = ll->count;
      if (idx1 < 0)
         idx1 = (-idx1 > i) ? 0 : idx1 + i + 1;
      if (idx2 < 0)
         idx2 = (-idx2 > i) ? 0 : idx2 + i + 1;
   }

   //Shorten all the file names, so that it is easy to read
   shorten_fnames(false);

   //Get the attributes for the different location hilite items. Note
   //that this depends on syntax items defined in the qf.vim syntax file
   fileDeco = decosByHiliteName(S"qfFileName");
   if (fileDeco.flags == 0)
      fileDeco = getFullDecoration(HLF_D);
   separatorDeco = decosByHiliteName(S"qfSeparator");
   if (separatorDeco.flags == 0)
      separatorDeco = getFullDecoration(HLF_D);
   lineDeco = decosByHiliteName(S"qfLineNr");
   if (lineDeco.flags == 0)
      lineDeco = getFullDecoration(HLF_N);

   if (ll->noValidEntries)
      all = true;
      
   LocLine* lline;
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if ((lline->isValid || all) && idx1 <= i && i <= idx2)
         displayListEntry(lline, i, i == ll->currentIdx);

      ui_breakcheck();
   }
   clearArrayList();
}

//Remove newlines and leading whitespace from an error message. Add the result to the list "gap"
private void
formatText(ArrayList *gap, CS text) {
   CS p = text;
   while (*p != ZERO) {
      if (*p == '\n') {
         ga_append(gap, ' ');
         while (*++p != ZERO) {
            if (!SPACE_OR_TAB(*p) && *p != '\n')
               break;
         } 
      } else
         ga_append(gap, *p++);
   }
}

//Add the range information from the lnum, col, end_lnum, and end_col values
//of a location entry to the grow array "gap".
private void
addRangeInformationToArrayList(ArrayList* gap, LocLine* lline) {
   CS builder = IObuff;
   int bufsize = IOSIZE;

   eeSnprintf(builder, bufsize, FMT_UNT, lline->lNum);
   int len = (int)STRLEN(builder);

   if (lline->endLNum > 0 && lline->lNum != lline->endLNum) {
      eeSnprintf(builder + len, bufsize - len, "-" FMT_UNT, lline->endLNum);
      len += (int)STRLEN(builder + len);
   }
   if (lline->col > 0) {
      eeSnprintf(builder + len, bufsize - len, " col %d", lline->col);
      len += (int)STRLEN(builder + len);
      if (lline->endCol > 0 && lline->col != lline->endCol) {
         eeSnprintf(builder + len, bufsize - len, "-%d", lline->endCol);
         len += (int)STRLEN(builder + len);
      }
   }

   ga_concat_len(gap, builder, len);
}

//Display information (list number, list size and the title) about a location list.
private void
qf_msg(LocationStack* stack, int which, CS lead) {
    CS title = stack->lists[which].title;
    int count = stack->lists[which].count;
    Byte builder[IOSIZE];

    eeSnprintf(
       builder, 
       IOSIZE, 
       _("%serror list %d of %d; %d errors "),
       lead,
       which + 1,
       stack->listcount,
       count
   );

   if (title) {
      Unt len = STRLEN(builder);
      if (len < 34) {
         memset(builder + len, ' ', 34 - len);
         builder[34] = ZERO;
      }
      concatenateStrings(builder, (CS)title, IOSIZE);
   }
   trunc_string(builder, builder, visibleColsG - 1, IOSIZE);
   msg(builder);
}

//":molder [count]": Up in the location stack. TODO remove
//":mnewer [count]": Down in the location stack.
//":lolder [count]": Up in the location list stack.
//":lnewer [count]": Down in the location list stack.
pub void
c_llAge(Invocation* invo) {
   LocationStack* stack;

   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   int count = (invo->addr_count != 0) ? invo->line2 : 1;
   while (count--) {
      if (invo->id == C_lolder) {
         if (stack->currList == 0) {
            emsg(_(e_at_bottom_of_quickfix_stack));
            break;
         }
         --stack->currList;
      } else {
         if (stack->currList + 1 >= stack->listcount) {
            emsg(_(e_at_top_of_quickfix_stack));
            break;
         }
         ++stack->currList;
      }
   }
   qf_msg(stack, stack->currList, S"");
   updateBook(stack, NULL);
}

//Display the information about all the location lists in the stack
pub void
qf_history(Invocation* invo) {
   LocationStack* stack = getStackForCommand(invo, false);

   if (invo->addr_count > 0) {
      if (!stack) {
          emsg(_(e_no_location_stack));
          return;
      }

      //Jump to the specified location list
      if (invo->line2 > 0 && invo->line2 <= (int)stack->listcount) {
          stack->currList = invo->line2 - 1;
          qf_msg(stack, stack->currList, S"");
          updateBook(stack, NULL);
      } else
          emsg(_(e_invalid_range));

      return;
   }

   if (isStackEmpty(stack))
      msg(_("No entries"));
   else {
      for (Unt i = 0; i < stack->listcount; ++i)
          qf_msg(stack, i, i == stack->currList ? S"> " : S"  ");
   } 
}

//Free all the entries in the error list "idx". Note that other information
//associated with the list like context and title are not freed.
private void
freeItems(LocationList* ll) {
   LocLine* lline;
   LocLine* nextLine;
   int      stop = false;

   while (ll->count && ll->first != NULL) {
      lline = ll->first;
      nextLine = lline->next;
      if (!stop) {
         eeglFree(lline->fName);
         eeglFree(lline->moduleName);
         eeglFree(lline->text);
         eeglFree(lline->pattern);
         clearVar(&lline->userData);
         stop = (lline == nextLine);
         eeglFree(lline);
         if (stop)
            //Somehow count may have an incorrect value, set it to 1
            //to avoid crashing when it's wrong.
            //TODO: Avoid count being incorrect.
            ll->count = 1;
         else
            ll->first = nextLine;
      }
      --ll->count;
   }

   ll->currentIdx = 0;
   ll->first = NULL;
   ll->last = NULL;
   ll->curr = NULL;
   ll->noValidEntries = true;

   qf_clean_dir_stack(&ll->dirStack);
   ll->dir = NULL;
   qf_clean_dir_stack(&ll->fileStack);
   ll->currFName = NULL;
   ll->qf_multiline = false;
   ll->qf_multiignore = false;
   ll->qf_multiscan = false;
}

//Free location list "idx". Frees all the entries, associated context information and the title.
private void
freeAList(LocationList* ll) {
   freeItems(ll);

   EE_CLEAR(ll->title);
   freeVar(ll->qf_ctx);
   ll->qf_ctx = NULL;
   evFreeCallback(&ll->textFn);
   ll->id = 0;
   ll->changedTick = 0L;
}

//Adjust entries between two lines of curBook by an amount. This is analogous to adjusting marks 
//and must happen simultaneously.
private void
llAdjustEntries(
   LineNr line1,
   LineNr line2,
   long amount, //how much to adjust entries in [line1; line2]. If == MAXLNUM, lines are deleted
   long amount_after //amount to adjust entries in tail lines (line2; ...)
){
   Boole isBufferLinked = false;

   if (!(curBook->hasLocationEntry))
      return;
      
   for (Unt i = 0; i < COUNT_LOC_LISTS; i++) {
      LocationStack* st = locationStacksP + i;
      for (Unt lInd = 0; lInd < st->listcount; ++lInd) {
         LocationList* ll = getList(st, lInd);

         if (isEmpty(ll)) {
            continue;
         }
         LocLine* lline;
         int j;
         FOR_ALL_LL_ITEMS(ll, lline, j) {
            if (lline->fNum == curBook->fiNum) {
               isBufferLinked = true;
               if (lline->lNum >= line1 && lline->lNum <= line2) {
                  if (amount == MAXLNUM)
                     lline->isCleared = true;
                  else
                     lline->lNum += amount;
               } ei (amount_after && lline->lNum > line2)
                  lline->lNum += amount_after;
            }
         } 
      }
   }

   if (!isBufferLinked)
      curBook->hasLocationEntry = false;
}

//Make a nice message out of the error character and the error number:
//char    number   message
//e or E    0      " error"
//w or W    0      " warning"
//i or I    0      " info"
//n or N    0      " note"
//0         0      ""
//other     0      " c"
//e or E    n      " error n"
//w or W    n      " warning n"
//i or I    n      " info n"
//n or N    n      " note n"
//0         n      " error n"
//other     n      " c n"
//1         x      ""   :helpgrep
private CS
createMsg(int c, int nr) {
   static Byte builder[20];
   static Byte cc[3];
   
   CS p;
   if (c == 'W' || c == 'w')
      p = S" warning";
   ei (c == 'I' || c == 'i')
      p = S" info";
   ei (c == 'N' || c == 'n')
      p = S" note";
   ei (c == 'E' || c == 'e' || (c == 0 && nr > 0))
      p = S" error";
   ei (c == 0 || c == 1)
      p = S"";
   else {
      cc[0] = ' ';
      cc[1] = c;
      cc[2] = ZERO;
      p = cc;
   }

   if (nr <= 0)
      return p;

   sprintf((char *)builder, "%s %3d", (char *)p, nr);
   return builder;
}

//When "split" is false: Open the entry/result under the cursor.
//When "split" is true: Open the entry/result under the cursor in a new portal.
pub void
llViewLocation(int split) {
   LocationStack* stack;
   if (IS_LL_PORTAL(curPor))
      stack = curPor->locationStackRef;
   else {
      emsg(_(e_no_location_stack));
      return;
   }

   if (isEmpty(getCurrent(stack))) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   if (split) {
      //Open the selected entry in a new portal
      jumpToNewPortal(stack, 0, (long)curPor->cursor.lnum, false, true);
      executeCommLine((CS) "clearjumps");
      return;
   }

   executeCommLine((CS)(IS_LL_PORTAL(curPor) ? ".ll" : ".mc"));
}

//":mwindow": open the location portal if we have errors to display, close it if not. TODO delete
//":lwindow": open the location list portal if we have locations to display, close it if not.
pub void
c_cPortal(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   LocationList* ll = getCurrent(stack);

   //Look for an existing location portal.
   Portal* po = findPortalIntoLocList(stack);

   //If a location portal is open but we have no errors to display, close the portal. If a 
   //location portal is not open, then open it if we have errors; otherwise, leave it closed.
   if (isStackEmpty(stack)
       || ll->noValidEntries
       || isEmpty(ll)
   ) {
     if (po)
         c_lClose(invo);
   } ei (!po)
      c_lOpen(invo);
}

//":lclose": close the window showing the location list
pub void
c_lClose(Invocation* invo) {
   LocationStack   *stack;
   if ((stack = getStackForCommand(invo, false)) == NULL)
      return;

   //Find existing location portal and close it.
   Portal* port = findPortalIntoLocList(stack);
   if (port != NULL)
      closePortal(port, false);
}

//Set "w:quickfix_title" if "stack" has a title.
private void
setTitleVar(LocationList* ll) {
   if (ll->title != NULL)
      set_internal_string_var((CS)"w:quickfix_title", ll->title);
}

//Go to a location list portal (if present).
//Return OK if the window is found, FAIL otherwise.
private int
gotoLocationPortal(LocationStack* stack, int resize, int sz, int vertsplit) {
   Portal* port = findPortalIntoLocList(stack);
   if (!port)
      return FAIL;

   gotoPortal(port);
   if (resize) {
      if (vertsplit) {
         if (sz != (int)port->width)
            portSetHeight(sz, curPor);
      } ei (sz != (int)port->height && port->height + STATUS_HEIGHT < commlineRowG)
         portSetHeight(sz, curPor);
   }

   return OK;
}

//Set options for the book in the location list portal.
private void
setPortalOptions() {
   //switch off 'swapfile'
   optChangeAndReportError(
      S"swapfile", (OptionValue){.tag = OPTION_BOOLE, .boole = false}, SET_LOCAL
   );
   optChangeAndReportError(
      S"booktype", (OptionValue){.tag = OPTION_STRING, .string = S"location"}, SET_LOCAL
   );
   optChangeAndReportError(
      S"bufhdden", (OptionValue){.tag = OPTION_STRING, .string = S"hide"}, SET_LOCAL
   );
   curPor->o.diff = false;
   optChangeAndReportError(
      S"foldmethod", (OptionValue){.tag = OPTION_STRING, .string = S"manual"}, SET_LOCAL
   );
}

//Open a new location list portal, load the location book and set the appropriate options for the 
//portal. Return FAIL if the portal could not be opened.
private int
openNewPortal(LocationStack* stack, int height) {
   Portal* oldPort = curPor;
   Tab* prevtab = curtab;
   Unt flags = 0;

   Book* llBook = findLlBook(stack);

   //The current portal becomes the previous one afterwards.
   Portal* port = curPor;

   if (commModifierG.cmod_split == 0)
      //Create the new location portal at the very bottom, except when
      //:belowright or :aboveleft is used.
      gotoPortal(lastPor);
      
   //Default is to open the portal below the current portal
   if (commModifierG.cmod_split == 0)
      flags = WSP_BELOW;
      
   flags |= WSP_NEWLOC;
   if (splitPortal(height, flags) == FAIL)
      return FAIL;      //not enough room for portal
      
   curPor->o.diff = false;

   //For the location list portal, create a reference to the
   //location list stack from the portal 'port'.
   curPor->locationStackRef = stack;
   stack->refCount++;

   if (oldPort != curPor)
      oldPort = NULL;  //don't store info when in another portal
   if (llBook != NULL) {
      //Use the existing location buffer
      if (startEditingFile(llBook->fiNum, NULL, NULL, NULL, ECMD_ONE,
             ECMD_HIDE + ECMD_OLDBUF + ECMD_NOWINENTER, oldPort) == FAIL)
         return FAIL;
   } else {
      //Create a new location buffer
      if (startEditingFile(0, NULL, NULL, NULL, ECMD_ONE, ECMD_HIDE + ECMD_NOWINENTER,
                               oldPort) == FAIL)
         return FAIL;

      //save the number of the new buffer
      stack->bufNum = curBook->fiNum;
   }

   //Set the options for the location buffer/portal (if not already done)
   //Do this even if the location buffer was already present, as an autocmd
   //might have previously deleted (:bdelete) the location buffer.
   if (!isLocationListBook(curBook))
      setPortalOptions();

   //Only set the height when still in the same tab and there is no portal to the side.
   if (curtab == prevtab && curPor->width == visibleColsG)
      portSetHeight(height, curPor);
   curPor->o.portFixHeight = true;       //set 'winfixheight'
   if (portalIsValid(port))
      prevPor = port;

   return OK;
}

//":lopen": open a window that shows the location list.
pub void
c_lOpen(Invocation* invo) {
   LocationStack* stack;
   int      status = FAIL;

   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   incrementLlBusyness();

   int height;
   if (invo->addr_count != 0)
      height = invo->line2;
   else
      height = QF_WINHEIGHT;

   reset_VIsual_and_resel();         //stop Visual mode

   //Find an existing location portal, or open a new one.
   if (commModifierG.cmod_tab == 0)
      status = gotoLocationPortal(stack, invo->addr_count != 0, height, commModifierG.cmod_split & WSP_VERT);
   if (status == FAIL) {
      if (openNewPortal(stack, height) == FAIL) {
         decrementLlBusyness();
         return;
      }
   }
   LocationList* ll = getCurrent(stack);
   setTitleVar(ll);
   //Save the current index here, as updating the location buffer may free the location list
   int lnum = ll->currentIdx;

   //Fill the buffer with the location list.
   fillBookWithLocList(ll, curBook, NULL, curPor->id);

   decrementLlBusyness();

   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;
   check_cursor();
   update_topline();      //scroll to show the line
}

//Move the cursor in the location portal to "lnum".
private void
gotoLine(Portal* po, LineNr lnum) {
   Portal* old_curPor = curPor;
   curPor = po;
   curBook = po->book;
   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;
   curPor->cursor.coladd = 0;
   curPor->cursWant = 0;
   update_topline();      //scroll to show the line
   redraw_later(UPD_VALID);
   curPor->statusLineNeedsRedraw = true;   //update ruler
   
   curPor = old_curPor;
   curBook = curPor->book;
}

 //:mbottom/:lbottom commands.
pub void
c_lBottom(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   Portal* po = findPortalIntoLocList(stack);
   if (po && po->cursor.lnum != po->book->mem.lineCount)
      gotoLine(po, po->book->mem.lineCount);
}

//Return the line number of the current entry in its location portal.
//Precondition: it's a location portal.
pub LineNr
llCurrentEntry(Portal* po) {
   return getCurrent(po->locationStackRef)->currentIdx;
}

//Update the cursor position in the location portal to the current error.
//Return true if there is a location portal.
private Boole
updatePortalPos(LocationStack* stack, int      old_currentIdx) {   //previous currentIdx or zero
   int currentIdx = getCurrent(stack)->currentIdx;

   //Put the cursor on the current error in the location portal, so that it's viewable.
   Portal* port = findPortalIntoLocList(stack);
   if (port != NULL
       && currentIdx <= port->book->mem.lineCount
       && old_currentIdx != currentIdx
   ) {
      if (currentIdx > old_currentIdx) {
         port->redrawTop = old_currentIdx;
         port->redrawBott = currentIdx;
      } else {
         port->redrawTop = currentIdx;
         port->redrawBott = old_currentIdx;
      }
      gotoLine(port, currentIdx);
   }
   return port != NULL;
}

//Check whether the given portal is displaying the specified location stack.
private Boole
isLocListPortal(Portal* port, LocationStack* stack) {
   //A portal displaying the location buffer will have the locationStackRef field set to NULL.
   //A portal displaying a location list buffer will have the locationStackRef
   //pointing to the location list.
   if (bookIsValid(port->book) 
         && isLocationListBook(port->book) && (port->locationStackRef == stack)
   )
      return true;

   return false;
}

//Find a portal into the location stack 'stack' in the current tab.
private Portal *
findPortalIntoLocList(LocationStack* stack) {
   Portal* port;
   FOR_ALL_PORTALS(port) {
      if (isLocListPortal(port, stack))
         return port;
   } 
   return NULL;
}

//Find a location buffer. Searches in open portals in all the tabs.
private Book*
findLlBook(LocationStack* stack) {
   if (stack->bufNum != INVALID_LL_BUFNR) {
      Book* llBook = bookFindFileByBookNr(stack->bufNum);
      if (llBook)
         return llBook;
      //buffer is no longer present
      stack->bufNum = INVALID_LL_BUFNR;
   }

   Portal* po;
   Tab* t;
   FOR_ALL_TAB_PORTALS(t, po) {
      if (isLocListPortal(po, stack))
         return po->book;
   } 

   return NULL;
}

//Process the 'quickfixtextfunc' option value. Returns OK or FAIL.
pub CS
setQuickfixtextfunc(OptionChange* cha) {
   CS new = cha->newVal.string;
   if (optSetCallback(OUT &locationTextFnS, new) == FAIL)
      return e_invalid_argument;
   p_qftf = new; 

   return NULL;
}

//Update the w:quickfix_title variable in the location list portal in all the tabs.
private void
updateTitleVar(LocationStack* stack) {
   LocationList* ll = getCurrent(stack);
   Tab* t;
   Portal* port;
   Portal* savedPor = curPor;

   FOR_ALL_TAB_PORTALS(t, port) {
      if (isLocListPortal(port, stack)) {
         curPor = port;
         setTitleVar(ll);
      }
   }
   curPor = savedPor;
}

//Find the location book. If it exists, update the contents.
private void
updateBook(LocationStack* stack, LocLine* oldLast) {
   //Check if a book for the location list exists. Update it.
   Book* book = findLlBook(stack);
   if (!book)
      return;

   LineNr old_line_count = book->mem.lineCount;
   int getLlPortalId = 0;

   Portal* port = findPortalIntoLocList(stack);
   if (!port)
      return;
      
   getLlPortalId = port->id;

   //autocommands may cause trouble
   incrementLlBusyness();

   int doFill = true;
   AutocommSave aco;
   if (oldLast == NULL) {
      //set curPor/curBook to book and save a few things
      auCommPrepareBook(&aco, book);
      if (curBook != book)
         doFill = false;  //failed to find a portal into "book"
   }

   if (doFill) {
      updateTitleVar(stack);

      fillBookWithLocList(getCurrent(stack), book, oldLast, getLlPortalId);
      ++CHANGEDTICK(book);

      if (!oldLast) {
         (void)updatePortalPos(stack, 0);

         //restore curPor/curBook and a few other things
         auCommRestoreBook(&aco);
      }
   }

   //Only redraw when added lines are visible. This avoids flickering
   //when the added lines are not visible.
   if ((port = findPortalIntoLocList(stack)) != NULL && old_line_count < port->bottomLine)
      drawBookLater(book, UPD_NOT_VALID);

   //always called after incrementLlBusyness()
   decrementLlBusyness();
}

//Add an error line to the loc list book.
private inline int
addLine(
   Book* book,      //location portal's book
   LineNr   lnum,
   LocLine* lline,
   CS dirname,
   Boole  firstBookLine,
   CS qftf_str
){
   Book* errBook;
   ArrayList   *gap;

   gap = getTempList();

   //If the 'quickfixtextfunc' returned a non-empty custom string for this entry, then use it
   if (qftf_str != NULL && *qftf_str != ZERO) {
      ga_concat(gap, qftf_str);
   } else {
      if (lline->moduleName != NULL)
         ga_concat(gap, lline->moduleName);
      ei (lline->fNum != 0
            && (errBook = bookFindFileByBookNr(lline->fNum)) != NULL
            && errBook->currFileName
      ){
         if (lline->kind == 1)   //:helpgrep
            ga_concat(gap, fiGetShortFiName(errBook->currFileName));
         else {
            //Shorten the file name if not done already.
            //For optimization, do this only for the first entry in a buffer.
            if (firstBookLine 
                  && (errBook->shortFileName == NULL || !strIsRelative(errBook->shortFileName))
            ){
               if (*dirname == ZERO)
                  mch_dirname(dirname, MAXPATHL);
               bookShortenName(errBook, dirname, false);
            }
            if (lline->fName == NULL)
               ga_concat(gap, errBook->currFileName);
            else
               ga_concat(gap, lline->fName);
          }
      }

      ga_append(gap, '|');

      if (lline->lNum > 0) {
         addRangeInformationToArrayList(gap, lline);
         ga_concat(gap, createMsg(lline->kind, lline->errNum));
      } ei (lline->pattern)
         formatText(gap, lline->pattern);
      ga_append(gap, '|');
      ga_append(gap, ' ');

      //Remove newlines and leading whitespace from the text. For an unrecognized line keep the 
      //indent, the compiler may mark a word with ^^^^.
      formatText(gap, gap->len > 3 ? skipwhite(lline->text) : lline->text);
   }

   ga_append(gap, ZERO);
   if (memAppendBook(book, lnum, gap->c, gap->len, false) == FAIL)
      return FAIL;

   return OK;
}

//Call the 'quickfixtextfunc' function to get the list of lines to display in the location portal 
//for the entries 'start_idx' to 'end_idx'.
private List *
callLocListToText(LocationList *ll, int getLlPortalId, long start_idx, long end_idx) {
   Callback   *cb = &locationTextFnS;
   List   *qftf_list = NULL;
   static int recursive = false;

   if (recursive)
      return NULL;  //this doesn't work properly recursively
   recursive = true;

   //If 'quickfixtextfunc' is set, then use the user-supplied function to get the text to display.
   //Use the local value of 'quickfixtextfunc' if it is set.
   if (ll->textFn.name != NULL)
      cb = &ll->textFn;
   if (cb->name != NULL) {
      Var args[1];
      Bag* d;
      Var returnVar;

      //create the dict argument
      if ((d = allocBag_lock(VAR_FIXED)) == NULL) {
         recursive = false;
         return NULL;
      }
      bagAddNumber(d, S"winid", (long)getLlPortalId);
      bagAddNumber(d, S"id", (long)ll->id);
      bagAddNumber(d, S"start_idx", start_idx);
      bagAddNumber(d, S"end_idx", end_idx);
      ++d->refCount;
      args[0].tag = VAR_BAG;
      args[0].bag = d;

      qftf_list = NULL;
      if (call_callback(cb, 0, &returnVar, 1, args) != FAIL) {
         if (returnVar.tag == VAR_LIST) {
            qftf_list = returnVar.list;
            qftf_list->refCount++;
         }
         clearVar(&returnVar);
      }
      bagUnref(d);
   }

   recursive = false;
   return qftf_list;
}

//Fill current buffer with location entries, replacing any previous contents curBook must be the 
//location buffer! If "oldLast" is not NULL append the items after this one. When "oldLast" is 
//NULL then "book" must equal "curBook"! Because ml_delete() is used and autocommands will be run.
private void
fillBookWithLocList(LocationList *ll, Book* book, LocLine *oldLast, int getLlPortalId) {
   LineNr lnum;
   LocLine* lline;
   int keyTypedSave = keyWasTypedG;
   List* locList = NULL;
   ListItem* listItem = NULL;

   if (!oldLast) {
      if (book != curBook) {
         internal_error(S"fillBookWithLocList()");
         return;
      }

      //delete all existing lines
      //
      //Note: we cannot store undo information, because
      //ll book is usually not allowed to be modified.
      //
      //So we need to clean up undo information
      //otherwise autocommands may invalidate the undo stack
      while ((curBook->mem.flags & ML_EMPTY) == 0)
         (void)ml_delete((LineNr)1);

      Portal* wp;
      Tab* t;
      FOR_ALL_TAB_PORTALS(t, wp) {
         if (wp->book == curBook)
            wp->skipCol = 0;
      } 

      //Remove all undo information
      invalidateUndoBufferAndFreeBlocks(curBook);
   }

   //Check if there is anything to display
   if (ll && ll->first) {
      Byte dirname[MAXPATHL];
      int invalid_val = false;
      int prev_bufnr = -1;

      dirname[0] = ZERO;

      //Add one line for each error
      if (!oldLast) {
         lline = ll->first;
         lnum = 0;
      } else {
         if (oldLast->next)
            lline = oldLast->next;
         else
           lline = oldLast;
         lnum = book->mem.lineCount;
      }

      locList = callLocListToText(ll, getLlPortalId, (long)(lnum + 1), (long)ll->count);
      if (locList)
         listItem = locList->first;

      while (lnum < ll->count) {
         CS str = NULL;

         //Use the text supplied by the user defined function (if any).
         //If the returned value is not string, then ignore the rest
         //of the returned values and use the default.
         if (listItem && !invalid_val) {
            str = convertVarToStringSingleUse(&listItem->c);
            if (!str)
               invalid_val = true;
         }

         if (addLine(book, lnum, lline, dirname, prev_bufnr != lline->fNum, str) == FAIL)
            break;

         prev_bufnr = lline->fNum;
         ++lnum;
         lline = lline->next;
         if (!lline)
            break;

         if (listItem)
            listItem = listItem->next;
      }

      if (!oldLast)
         //Delete the empty line which is now at the end
         (void)ml_delete(lnum + 1);

      clearArrayList();
   }

   //correct cursor position
   check_lnums(true);

   if (!oldLast) {
      //Set the book kind to "location" each time after filling the book.
      //This resembles reading a file into a book, it's more logical when using autocommands.
      curBook->kind = BOOK_LOCATION;
      curBook->keepFiletype = true;   //don't detect 'filetype'
      ++curBookLock;
      applyAutocomms(EVENT_BUFREADPOST, S"quickfix", NULL, false, curBook);
      applyAutocomms(EVENT_BUFWINENTER, S"quickfix", NULL, false, curBook);
      curBook->keepFiletype = false;
      --curBookLock;

      //make sure it will be redrawn
      drawCurBookLater(UPD_NOT_VALID);
   }

   //Restore keyWasTypedG, setting 'filetype' may reset it.
   keyWasTypedG = keyTypedSave;
}

//For every change made to the location list, update the changed tick.
private void
updateChangedTick(LocationList* ll) {
   ll->changedTick++;
}

//Return the location list number with the given identifier. Returns -1 if list is not found.
private int
idToNr(LocationStack* stack, Unt listId) {
   for (Unt ind = 0; ind < stack->listcount; ind++) {
      if (stack->lists[ind].id == listId)
         return ind;
   } 
   return INVALID_LL_IND;
}

//If the current list is not "idSave" and we can find the list with that ID then make it the 
//current list. This is used when autocommands may have changed the current list.
//Return OK if successfully restored the list. Return FAIL if the list with the specified 
//identifier (idSave) is not found in the stack.
private int
restoreList(LocationStack* stack, Unt idSave){
   if (getCurrent(stack)->id == idSave)
      return OK;

   int curlist = idToNr(stack, idSave);
   if (curlist < 0)
      //list is absent
      return FAIL;
   stack->currList = curlist;
   return OK;
}

//Jump to the first entry if there is one.
private void
jumpToFirstEntry(LocationStack* stack, Unt idSave, Boole forceit) {
   if (restoreList(stack, idSave) == FAIL)
      return;

   if (!portCheckCanSetCurBookForceIt(forceit))
      return;

   //Autocommands might have cleared the list, check for that.
   if (!isEmpty(getCurrent(stack)))
      llJump(stack, 0, 0, forceit);
}

//Return true when using ":vimgrep" for ":grep".
pub int
grepIsActuallyInternal(CommIndex id) {
   return (id == C_grep || id == C_grepadd) 
      && curBook->o.grepProg && eq(S"internal", curBook->o.grepProg);
}

//Return the grep autocmd name.
private NULLABLE CS
getGrepAutocommand(CommIndex id) {
   switch (id) {
   case C_grep:       return (CS)"grep";
   case C_grepadd:   return (CS)"grepadd";
   case C_elck:   return (CS)"elck";
   default: return NULL;
   }
}

//Return the name for the errorfile, in allocated memory. Find a new unique name when 
//@makeef contains "##". Return NULL for error.
private Arr(Byte)
buildErrorFileName(void) {
   static int start = -1;
   static int off = 0;
   FileStat sb;

   CS name;
   if (!p_mef) {
      name = eeTempName('e', false);
      if (!name)
         emsg(_(e_cant_get_temp_file_name));
      return name;
   }

   CS p;
   for (p = p_mef; *p != ZERO; ++p) {
      if (p[0] == '#' && p[1] == '#')
         break;
   } 

   if (*p == ZERO)
      return copyStr(p_mef);

   //Keep trying until the name doesn't exist yet.
   for (;;) {
      if (start == -1)
         start = mch_get_pid();
      else
         off += 19;

      name = alloc_id(STRLEN(p_mef) + 30, aid_ll_mef_name);
      if (!name)
         break;
      STRCPY(name, p_mef);
      sprintf((char *)name + (p - p_mef), "%d%d", start, off);
      STRCAT(name, p + 2);
      if (mch_getperm(name) < 0
             //Don't accept a symbolic link, it's a security risk.
             && lstat((char *)name, &sb) < 0
         )
          break;
      eeglFree(name);
   }
   return name;
}

//Form the complete command line to invoke 'make'/'grep'. Quote and append @shellpipe. Echo the 
//fully formed command.
private CS
buildFullShellCommand(CS makecmd) {
   Unt len = STRLEN(makecmd) + 1;
   CS cmd = alloc_id(len, aid_ll_makecmd);
   SPRINTF(cmd, "%s", (char *)makecmd);

   //Display the fully formed command.  Output a newline if there's something
   //else than the :make command that was typed (in which case the cursor is in column 0).
   if (msgColG == 0)
      msg_didout = false;
   msg_start();
   msg_puts(S":!");
   msg_outtrans(cmd);      //show what we are doing

   return cmd;
}

//Process :eegl command arguments. The command syntax is:
//
// :{count}eegl /{pattern}/[g][j]
private int
eeglProcessArgs(Invocation* invo, OUT VimGrepArgs* args) {
   CLEAR_POINTER(args);

   args->regmatch.regprog = NULL;
   args->title = copyStr(copyCommandTitle(*invo->commline));

   if (invo->addr_count > 0)
      args->tomatch = invo->line2;
   else
      args->tomatch = MAXLNUM;

   //Get the search pattern: either white-separated or enclosed in //
   CS p = skipEeglGrepPat(invo->arg, &args->spat, &args->flags);
   if (!p) {
      emsg(_(e_invalid_search_pattern_or_delimiter));
      return FAIL;
   }

   vgr_init_regmatch(&args->regmatch, args->spat);
   if (args->regmatch.regprog == NULL)
      return FAIL;

   p = skipwhite(p);
   if (*p != ZERO) {
      emsg(_(e_trailing_characters_str));
      return FAIL;
   }

   return OK;
}

//Internal grep of all files, the "eegl" or "eegrep" commands.
//They search all files except the .git subfolder and put the results into a location list
pub void
c_elgrep(Invocation* invo) {
   VimGrepArgs args;
   LocationList* ll;
   CS target_dir = NULL;

   if (!portCheckCanSetCurBookForceIt(invo->forceit))
      return;

   CS auName = vgr_get_auname(invo->id);
   if (auName
         && applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()
   ) {
      return;
   }

   LocationStack* stack = locationStacksP + LOC_LIST_GREP;

   if (eeglProcessArgs(invo, OUT &args) == FAIL)
      goto theend;

   if ((invo->id != C_elckadd) || isStackEmpty(stack)) {
      //make place for a new list
      newLocList(stack, args.title);
   } 

   incrementLlBusyness();

   Book* firstMatchBook = NULL;
   Boole redrawForDummy = false;
   int status = elckGrepFiles(stack, &args, OUT &redrawForDummy, &firstMatchBook, OUT &target_dir);
   ExpandMatch m = (ExpandMatch){.c = args.fnames, .len = args.fcount, .a = createArena()};
   if (status != OK) {
      decrementLlBusyness();
      goto theend;
   }

   ll = getCurrent(stack);
   ll->noValidEntries = false;
   ll->curr = ll->first;
   ll->currentIdx = 1;
   updateChangedTick(ll);

   updateBook(stack, NULL);

   //Remember the current location list identifier, so that we can check for
   //autocommands changing the current list.
   Unt idSave = getCurrent(stack)->id;

   if (auName != NULL)
      applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
   //The QuickFixCmdPost autocmd may free the location list. Check the list is still valid.
   if (!isIdValid(stack, idSave) || restoreList(stack, idSave) == FAIL) {
      decrementLlBusyness();
      goto theend;
   }

   //Jump to first match.
   if (!isEmpty(getCurrent(stack))) {
      if ((args.flags & VGR_NOJUMP) == 0)
         jumpToFirstMatchAndUpdateDir(
               stack, invo->forceit, OUT &redrawForDummy, OUT firstMatchBook, target_dir
         );
   } else
      showErrFmtMsg(_(e_no_match_str_2), args.spat);

   decrementLlBusyness();

   //If we loaded a dummy buffer into the current portal, the autocommands
   //may have messed up things, need to redraw and recompute folds.
   if (redrawForDummy) {
      foldUpdateAll(curPor);
   }

theend:
   deleteArena(m.a);
   eeglFree(args.title);
   eeglFree(target_dir);
   eeRegFree(args.regmatch.regprog);
}

//Used for ":grep" and ":grepadd"
pub void
c_grep(Invocation* invo) {
   CS errorformat = curBook->o.errorFormat;
   Boole newlist = true;

   //Redirect ":grep" to ":vimgrep" if 'grepprg' is "internal".
   if (grepIsActuallyInternal(invo->id)) {
      c_vimgrep(invo);
      return;
   }

   CS auName = getGrepAutocommand(invo->id);
   if (auName
            && applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
            && aborting()
   ) {
      return;
   }

   doFlushAllBooks();
   CS fname = buildErrorFileName();
   if (!fname)
      return;
   mch_remove(fname);       //in case it's not unique

   CS comm = buildFullShellCommand(invo->arg);
   if (!comm) {
      eeglFree(fname);
      return;
   }

   do_shell(comm, 0);

   incrementLlBusyness();

   if (invo->id != C_make)
      errorformat =  curBook->o.grepFormat;
   if (invo->id == C_grepadd)
      newlist = false;
      
   LocationStack* stack = locationStacksP + LOC_LIST_GREP;
   int res = llInitFromFile(stack, fname, errorformat, newlist, copyCommandTitle(*invo->commline));

   //Remember the current location list identifier, so that we can
   //check for autocommands changing the current list.
   Unt llIdSaved = getCurrent(stack)->id;
   if (auName)
      applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
   if (res > 0 && !invo->forceit && isIdValid(stack, llIdSaved))
      //display the first error
      jumpToFirstEntry(stack, llIdSaved, false);

   decrementLlBusyness();
   mch_remove(fname);
   eeglFree(fname);
   eeglFree(comm);
}

//Initialize the location list for the in-progress "make" command
pub void initInProgressLl() {
   if (makeInProgressS) {
      list_free(makeInProgressS);
   }
   
   TypeSpec* stringSpec = ALLOC_ONE(TypeSpec);
   stringSpec->tag = VAR_STRING;
   stringSpec->args = NULL;
   TypeSpec* listSpec = ALLOC_ONE(TypeSpec);
   listSpec->args = NULL;
   
   listSpec->tag = VAR_LIST;
   listSpec->member = stringSpec;
   listSpec->args = NULL;
   makeInProgressS = ALLOC_CLEAR_ONE(List);
   makeInProgressS->ty = listSpec;
   
   if (makeInProgressS) {
      isMakeRunningS = 0;
   }
}

//Callback for a single error message from "make"
private void
makeReceiveMessage(Arr(Byte) msg) {
   Var newMessage = (Var){.tag = VAR_STRING, .lock = false, .string = msg };
   list_append_tv(makeInProgressS, &newMessage);
}

//The callback when "make" program returned its results
private void
makeFinished() {
   isMakeRunningS = false;
   Source source = (Source){
      .tag = SOURCE_LIST, .List = (ListSource){.c = makeInProgressS->first}
   };
   initAndUpdateTick(
      source, OUT locationStacksP + LOC_LIST_MAKE, curBook->o.errorFormat, true, S"make"
   );
   
   if (applyAutocomms(EVENT_QUICKFIXCMDPRE, S"make", curBook->currFileName, true, curBook) 
         && aborting()) {
      return;
   }
   
   if (makeOpenWhenDoneG) {
      Invocation invo;
      invo.comm = (CS)"lopen";
      invo.id = C_lopen;
      invo.line2 = 4;
      invo.arg = (CS)"m"; //the "make" location stack
      c_lOpen(&invo);
   } else {
      showNotification((CS)"make finished, use [m, ]m, :mopen");
   }
}

pub void
c_make(Invocation*) {
   if (applyAutocomms(EVENT_QUICKFIXCMDPRE, S"make", curBook->currFileName, true, curBook) 
         && aborting()
   ) {
      return;
   }
   
   if (isMakeRunningS) {
      showNotification((CS)"make is already running");
      return;
   }
   
   doFlushAllBooks();

   Var vars[1];
   vars[0] = *allocStringVar((CS)"bash -c make");
   JobOptions jobOpts = (JobOptions){
      .finishNativeCb = &makeFinished,
      .errNativeCb = &makeReceiveMessage,
   };
   startJob(vars, NULL, &jobOpts, NULL);  
   initInProgressLl();
}

//Returns the number of entries in the current location list.
pub int
llGetSize(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, false)) == NULL)
      return 0;
   return getCurrent(stack)->count;
}

//Returns the number of valid entries in the current location list.
pub int
llGetValidSize(Invocation* invo){
   LocationStack* stack;
   LocLine   *lline;
   int i, sz = 0;
   int prev_fnum = 0;

   if ((stack = getStackForCommand(invo, false)) == NULL)
      return 0;

   LocationList* ll = getCurrent(stack);
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if (lline->isValid) {
         if (invo->id == C_ldo)
            sz++;   //Count all valid entries
         ei (lline->fNum > 0 && lline->fNum != prev_fnum) {
            //Count the number of files
            sz++;
            prev_fnum = lline->fNum;
         }
      }
   }

   return sz;
}

//Return the current index of the location list. Return 0 if there is an error.
pub int
llGetCurrIndex(Invocation* invo) {
   LocationStack   *stack;

   if ((stack = getStackForCommand(invo, false)) == NULL)
      return 0;

   return getCurrent(stack)->currentIdx;
}

//Return the current index in the location list (counting only valid
//entries). If no valid entries are in the list, then return 1.
pub int
llGetCurrValidIndex(Invocation* invo) {
   LocationStack* stack;
   int      i, eidx = 0;
   int      prev_fnum = 0;

   if ((stack = getStackForCommand(invo, false)) == NULL)
      return 1;

   LocationList* ll = getCurrent(stack);
   LocLine* lline = ll->first;

   //check if the list has valid errors
   if (!listHasValidEntries(ll))
      return 1;

   for (i = 1; i <= ll->currentIdx && lline!= NULL; i++, lline = lline->next) {
      if (lline->isValid) {
         if (invo->id == C_lfdo) {
            if (lline->fNum > 0 && lline->fNum != prev_fnum) {
               //Count the number of files
               eidx++;
               prev_fnum = lline->fNum;
            }
         } else
            eidx++;
      }
   }

   return eidx ? eidx : 1;
}

//Get the 'n'th valid error entry in the location list.
//Used by :ldo and :lfdo commands.
//For :ldo returns the 'n'th valid error entry.
//For :lfdo returns the 'n'th valid file entry.
private int
nthValidEntry(LocationList* ll, int n, int fdo){
   int      i, eidx;
   int      prev_fnum = 0;

   //check if the list has valid errors
   if (!listHasValidEntries(ll))
      return 1;

   eidx = 0;
   LocLine* lline;
   FOR_ALL_LL_ITEMS(ll, lline, i) {
   if (lline->isValid) {
      if (fdo) {
         if (lline->fNum > 0 && lline->fNum != prev_fnum) {
            //Count the number of files
            eidx++;
            prev_fnum = lline->fNum;
         }
      } else
         eidx++;
   }

   if (eidx == n)
       break;
   }

   if (i <= ll->count)
      return i;
   else
      return 1;
}

//Location list movement. ":ll", ":lrewind", ":lfirst" and ":llast". ":ldo" and ":lfdo"
pub void
c_lMove(Invocation* invo) {
   LocationStack* stack;
   int      errornr;

   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   if (invo->addr_count > 0)
      errornr = (int)invo->line2;
   else {
      switch (invo->id) {
         case C_ll:
            errornr = 0;
            break;
         case C_lrewind:
         case C_lfirst:
            errornr = 1;
            break;
         default:
            errornr = 32767;
      }
   }

   //For cdo and ldo commands, jump to the nth valid error.
   //For cfdo and lfdo commands, jump to the nth valid file entry.
   if (invo->id == C_ldo || invo->id == C_lfdo) {
      errornr = nthValidEntry(
         getCurrent(stack),
         invo->addr_count > 0 ? (int)invo->line1 : 1,
         invo->id == C_lfdo
      );
   } 

   llJump(stack, 0, errornr, invo->forceit);
}

//":lnext", ":lNext", ":lprevious", ":lnfile", ":lNfile" and ":lpfile".
//Also, used by ":ldo" and ":lfdo" commands.
pub void
c_lNext(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   int errornr;
   if (invo->addr_count > 0 && (invo->id != C_ldo && invo->id != C_lfdo))
      errornr = (int)invo->line2;
   else
      errornr = 1;

   //Depending on the command jump to either next or previous entry/file.
   int dir;
   switch (invo->id) {
   case C_lnext: case C_ldo:
      dir = FORWARD;
      break;
   case C_lprevious:
      dir = BACKWARD;
      break;
   case C_lnfile: case C_lfdo:
      dir = FORWARD_FILE;
      break;
   case C_lpfile:
      dir = BACKWARD_FILE;
      break;
   default:
      dir = FORWARD;
      break;
   }

   llJump(stack, dir, errornr, invo->forceit);
}

//Find the first entry in the location list 'll' from buffer 'bnr'.
//The index of the entry is stored in 'errornr'.
//Return NULL if an entry is not found.
private LocLine *
findFirstEntryInBuf(LocationList* ll, int bnr, int* errornr){
   LocLine* lline = NULL;
   int idx = 0;

   //Find the first entry in this file
   FOR_ALL_LL_ITEMS(ll, lline, idx)
   if (lline->fNum == bnr)
      break;

   *errornr = idx;
   return lline;
}

//Find the first location entry on the same line as 'entry'. Updates 'errornr'
//with the error number for the first entry. Assumes the entries are sorted in
//the location list by line number.
private LocLine *
qf_find_first_entry_on_line(LocLine* entry, int* errornr) {
    while (!gotInterruptG
          && entry->prev
          && entry->fNum == entry->prev->fNum
          && entry->lNum == entry->prev->lNum) {
      entry = entry->prev;
      --*errornr;
   }

   return entry;
}

//Find the last location entry on the same line as 'entry'. Updates 'errornr'
//with the error number for the last entry. Assumes the entries are sorted in
//the location list by line number.
private LocLine *
qf_find_last_entry_on_line(LocLine* entry, int* errornr){
   while (!gotInterruptG && entry->next
          && entry->fNum == entry->next->fNum
          && entry->lNum == entry->next->lNum
   ) {
      entry = entry->next;
      ++*errornr;
   }

   return entry;
}

//Return true if the specified location entry is
// after the given line (linewise is true)
// or after the line and column.
private int
isEntryAfterPos(LocLine* lline, Pos* pos, int linewise){
   if (linewise)
      return lline->lNum > pos->lnum;
   else
      return (lline->lNum > pos->lnum || (lline->lNum == pos->lnum && lline->col > pos->col));
}

//Return true if the specified location entry is
//before the given line (linewise is true) or before the line and column.
private int
qf_entry_before_pos(LocLine *lline, Pos *pos, int linewise){
   if (linewise)
      return lline->lNum < pos->lnum;
   else
      return (lline->lNum < pos->lnum || (lline->lNum == pos->lnum && lline->col < pos->col));
}

//Return true if the specified location entry is on or after the given line (linewise is true)
//or on or after the line and column.
private int
qf_entry_on_or_after_pos(LocLine* lline, Pos* pos, int linewise){
   if (linewise)
      return lline->lNum >= pos->lnum;
   else
      return (lline->lNum > pos->lnum || (lline->lNum == pos->lnum && lline->col >= pos->col));
}

//Return true if the specified location entry is
//on or before the given line (linewise is true) or on or before the line and column.
private int
isEntryOnOrBeforePos(LocLine* lline, Pos* pos, int linewise) {
   if (linewise)
      return lline->lNum <= pos->lnum;
   else
      return (lline->lNum < pos->lnum || (lline->lNum == pos->lnum && lline->col <= pos->col));
}

//Find the first location entry after position 'pos' in buffer 'bnr'.
//If 'linewise' is true, return the entry after the specified line and treat multiple entries on a 
//single line as one. Otherwise returns the entry after the specified line and column.
//'lline' points to the very first entry in the buffer and 'errornr' is the index of the very 
//first entry in the location list. Return NULL if an entry is not found after 'pos'.
private LocLine*
findEntryAfterPos(
   int      bnr,
   Pos      *pos,
   int      linewise,
   LocLine   *lline,
   int      *errornr
){
   if (isEntryAfterPos(lline, pos, linewise))
      //First entry is after position 'pos'
      return lline;

   //Find the entry just before or at the position 'pos'
   while (lline->next != NULL
          && lline->next->fNum == bnr
          && isEntryOnOrBeforePos(lline->next, pos, linewise)
   ) {
      lline = lline->next;
      ++*errornr;
   }

   if (lline->next == NULL || lline->next->fNum != bnr)
      //No entries found after position 'pos'
      return NULL;

   //Use the entry just after position 'pos'
   lline = lline->next;
   ++*errornr;

   return lline;
}

//Find the first location entry before position 'pos' in buffer 'bnr'.
//If 'linewise' is true, returns the entry before the specified line and
//treats multiple entries on a single line as one. Otherwise returns the entry
//before the specified line and column.
//'lline' points to the very first entry in the buffer and 'errornr' is the
//index of the very first entry in the location list.
//Return NULL if an entry is not found before 'pos'.
private LocLine *
findEntryBeforePos(
   int bnr,
   Pos* pos,
   int linewise,
   LocLine* lline,
   int* errornr
) {
   //Find the entry just before the position 'pos'
   while (lline->next != NULL
       && lline->next->fNum == bnr
       && qf_entry_before_pos(lline->next, pos, linewise)
   ) {
      lline = lline->next;
      ++*errornr;
   }

   if (qf_entry_on_or_after_pos(lline, pos, linewise))
      return NULL;

   if (linewise)
      //If multiple entries are on the same line, then use the first entry
      lline = qf_find_first_entry_on_line(lline, errornr);

   return lline;
}

//Find a location entry in 'll' closest to position 'pos' in buffer 'bnr' in the direction 'dir'.
private LocLine *
findClosestEntry(
   LocationList* ll,
   int bnr,
   Pos* pos,
   int dir,
   int linewise,
   OUT int* errornr
) {
   *errornr = 0;

   //Find the first entry in this file
   LocLine* lline = findFirstEntryInBuf(ll, bnr, errornr);
   if (lline == NULL)
      return NULL;      //no entry in this file

   if (dir == FORWARD)
      lline = findEntryAfterPos(bnr, pos, linewise, lline, errornr);
   else
      lline = findEntryBeforePos(bnr, pos, linewise, lline, errornr);

   return lline;
}

//Get the nth location entry below the specified entry.  Searches forward in
//the list. If linewise is true, then treat multiple entries on a single line as one.
private void
getNthEntryBelow(LocLine *entry_arg, int n, int linewise, int *errornr) {
   LocLine *entry = entry_arg;

   while (n-- > 0 && !gotInterruptG) {
      int      first_errornr = *errornr;

      if (linewise)
          //Treat all the entries on the same line in this file as one
          entry = qf_find_last_entry_on_line(entry, errornr);

      if (entry->next == NULL
         || entry->next->fNum != entry->fNum)
      {
          if (linewise)
         *errornr = first_errornr;
          break;
      }

      entry = entry->next;
      ++*errornr;
    }
}

//Get the nth location entry above the specified entry.  Searches backwards in
//the list. If linewise is true, then treat multiple entries on a single line as one.
private void
getNthEntryAbove(LocLine *entry, int n, int linewise, int *errornr){
   while (n-- > 0 && !gotInterruptG) {
      if (entry->prev == NULL || entry->prev->fNum != entry->fNum)
         break;

      entry = entry->prev;
      --*errornr;

      //If multiple entries are on the same line, then use the first entry
      if (linewise)
         entry = qf_find_first_entry_on_line(entry, errornr);
   }
}

//Find the n'th location entry adjacent to position 'pos' in buffer 'bnr' in
//the specified direction.  Returns the error number in the location list or 0
//if an entry is not found.
private int
findNthAdjacentEntry(
   LocationList* ll,
   int bnr,
   Pos* pos,
   int n,
   int dir,
   int linewise
) {
   int      errornr;
   //Find an entry closest to the specified position
   LocLine* adj_entry = findClosestEntry(ll, bnr, pos, dir, linewise, OUT &errornr);
   if (!adj_entry)
      return 0;

   if (--n > 0) {
      //Go to the n'th entry in the current buffer
      if (dir == FORWARD)
         getNthEntryBelow(adj_entry, n, linewise, &errornr);
      else
         getNthEntryAbove(adj_entry, n, linewise, &errornr);
   }

   return errornr;
}

//Jump to a location entry in the current file nearest to the current line. ":labove", ":lbelow"
pub void
c_lBelow(Invocation* invo) {
   LocationStack*stack;
   Unt dir;
   int errornr = 0;

   if (invo->addr_count > 0 && invo->line2 <= 0) {
      emsg(_(e_invalid_range));
      return;
   }

   Boole isBufferLinked = true;
   if (!(curBook->hasLocationEntry && isBufferLinked)) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   if ((stack = getStackForCommand(invo, true)) == NULL)
      return;

   LocationList* ll = getCurrent(stack);
   //check if the list has valid errors
   if (!listHasValidEntries(ll)) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   if (invo->id == C_lbelow)
      //Forward motion commands
      dir = FORWARD;
   else
      dir = BACKWARD;

   Pos pos = curPor->cursor;
   //A location entry column number is 1 based whereas cursor column
   //number is 0 based. Adjust the column number.
   pos.col++;
   errornr = findNthAdjacentEntry(ll, curBook->fiNum, &pos,
            invo->addr_count > 0 ? invo->line2 : 0, dir,
            invo->id == C_lbelow || invo->id == C_labove
   );

   if (errornr > 0)
      llJump(stack, 0, errornr, false);
   else
      emsg(_(e_no_more_items));
}

//Return the autocmd name for the :mfile Commands
private CS
cfile_get_auname(CommIndex id){
   switch (id) {
   case C_lfile:       return S"lfile";
   case C_laddfile:  return S"laddfile";
   default:       return NULL;
   }
}

//":lfile"/":laddfile" commands.
pub void
c_lFile(Invocation* invo) {
   Unt   idSave = 0;      //init for gcc

   CS auName = cfile_get_auname(invo->id);
   if (auName && applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, NULL, false, curBook)
       && aborting()
   )
      return;

   if (*invo->arg != ZERO)
      optChangeStringOptionDirect(S"errorfile", invo->arg, 0, 0);
   if (!p_ef) {
      return;
   }
   
   LocationStack* stack = identifyStackByInvo(invo);
   if (stack == NULL) {
      emsg(_(e_no_location_stack));
      return;
   }
   
   incrementLlBusyness();
   //This function is used by the :mfile and :maddfile commands.
   //:mfile always creates a new location list and may jump to the first entry.
   //:maddfile adds to an existing location list. If there is no
   //location list then a new list is created.
   int res = llInitFromFile(
      stack, p_ef, curBook->o.errorFormat, (invo->id != C_laddfile), 
      copyCommandTitle(*invo->commline)
   );
   
   if (res >= 0)
      updateChangedTick(getCurrent(stack));
   idSave = getCurrent(stack)->id;
   if (auName != NULL)
      applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, NULL, false, curBook);

   //Jump to the first error for a new list and if autocmds didn't free the list
   if (res > 0 && (invo->id == C_lfile) && isIdValid(stack, idSave))
      //display the first error
      jumpToFirstEntry(stack, idSave, invo->forceit);

   decrementLlBusyness();
}

//Return the vimgrep autocmd name.
private CS
vgr_get_auname(CommIndex id) {
   switch (id) {
   case C_vimgrep:     return (CS)"vimgrep";
   case C_vimgrepadd:  return (CS)"vimgrepadd";
   case C_grep:        return (CS)"grep";
   case C_grepadd:     return (CS)"grepadd";
   case C_elck:        return (CS)"elck";
   default:              return NULL;
   }
}

//Initialize the regmatch used by vimgrep for pattern "s".
private void
vgr_init_regmatch(RegMultilineMatch* regmatch, CS s) {
   //Get the search pattern: either white-separated or enclosed in //
   regmatch->regprog = NULL;

   if (s == NULL || *s == ZERO) {
      //Pattern is empty, use last search pattern.
      if (last_search_pat().len == 0) {
          emsg(_(e_no_previous_regular_expression));
          return;
      }
      regmatch->regprog = compileRegexp(last_search_pat().c, RE_MAGIC);
   } else
      regmatch->regprog = compileRegexp(s, RE_MAGIC);

   regmatch->rmm_ic = p_ic;
   regmatch->rmm_maxcol = 0;
}

//Display a file name when vimgrep is running.
private void
vgr_display_fname(Byte *fname) {
   msg_start();
   CS p = msg_strtrunc(fname, true);
   if (p == NULL)
      msg_outtrans(fname);
   else {
      msg_outtrans(p);
      eeglFree(p);
   }
   msg_clr_eos();
   msg_didout = false;       //overwrite this message
   msg_nowait = true;       //don't wait for this message
   msgColG = 0;
   out_flush();
}

//Load a dummy book to search for a pattern using vimgrep.
private Book*
vgr_load_dummy_book(CS fname, CS dirname_start, CS dirname_now) {
   //Don't do Filetype autocommands to avoid loading syntax and
   //indent scripts, a great speed improvement.
   CS save_ei = au_event_disable(S",Filetype");

   //Load file into a book, so that autocommands applied etc.
   Book* book = loadDummyBook(fname, dirname_start, dirname_now);

   au_event_restore(save_ei);

   return book;
}

//Check whether a location list is valid. Autocmds may remove or change a location list when 
//vimgrep is running. If the list is not found, create a new list
private int
vgr_isIdValid(LocationStack* stack, Unt listId, CS title){
   //Verify that the location list was not freed by an autocmd
   if (!isIdValid(stack, listId)) {
      newLocList(stack, title);
   }

   if (restoreList(stack, listId) == FAIL)
      return false;

   return true;
}

//Search for a pattern in all the lines in a buffer and add the matching lines to a location list.
private int
vgr_match_buflines(
   LocationList* ll,
   CS fname,
   Book* book,
   CS spat,
   RegMultilineMatch* regmatch,
   long* tomatch,
   int duplicate_name,
   int flags
) {
   int      found_match = false;
   long   lnum;
   ColNr   col;
   int      pat_len = (int)STRLEN(spat);
   if (pat_len > FUZZY_MATCH_MAX_LEN)
      pat_len = FUZZY_MATCH_MAX_LEN;

   for (lnum = 1; lnum <= book->mem.lineCount && *tomatch > 0; ++lnum) {
      col = 0;
      if (!(flags & VGR_FUZZY)) {
         //Regular expression match
         while (eeRegexec_multi(regmatch, curPor, book, lnum, col, NULL) > 0) {
         //Pass the book number so that it gets used even for a dummy book, unless duplicate_name 
         //is set, then the book will be wiped out below.
         if (addEntry(ll,
                NULL,   //dir
                fname,
                NULL,
                duplicate_name ? 0 : book->fiNum,
                memGetLine(book, regmatch->startpos[0].lnum + lnum, false),
                regmatch->startpos[0].lnum + lnum,
                regmatch->endpos[0].lnum + lnum,
                regmatch->startpos[0].col + 1,
                regmatch->endpos[0].col + 1,
                false,   //vis_col
                NULL,   //search pattern
                0,      //nr
                0,      //type
                NULL,   //user_data
                true   //valid
                ) == QF_FAIL)
         {
             gotInterruptG = true;
             break;
         }
         found_match = true;
         if (--*tomatch == 0)
            break;
         if ((flags & VGR_GLOBAL) == 0 || regmatch->endpos[0].lnum > 0)
            break;
         col = regmatch->endpos[0].col + (col == regmatch->endpos[0].col);
         if (col > memGetBookLen(book, lnum))
            break;
         }
      } else {
         Byte  *str = memGetLine(book, lnum, false);
         ColNr linelen = memGetBookLen(book, lnum);
         int       score;
         Unt   matches[FUZZY_MATCH_MAX_LEN];
         Unt   sz = ARRAY_LENGTH(matches);

         //Fuzzy string match
         CLEAR_FIELD(matches);
         while (fuzzy_match(str + col, spat, false, &score, matches, sz) > 0) {
            //Pass the book number so that it gets used even for a dummy book, unless 
            //duplicate_name is set, then the book will be wiped out below.
            if (addEntry(ll,
                   NULL,   //dir
                   fname,
                   NULL,
                   duplicate_name ? 0 : book->fiNum,
                   str,
                   lnum,
                   0,
                   matches[0] + col + 1,
                   0,
                   false,   //vis_col
                   NULL,   //search pattern
                   0,      //nr
                   0,      //type
                   NULL,   //user_data
                   true   //valid
                   ) == QF_FAIL)
            {
                gotInterruptG = true;
                break;
            }
            found_match = true;
            if (--*tomatch == 0)
                break;
            if ((flags & VGR_GLOBAL) == 0)
                break;
            col = matches[pat_len - 1] + col + 1;
            if (col > linelen)
                break;
          }
      }
      line_breakcheck();
      if (gotInterruptG)
          break;
    }

    return found_match;
}

private void
jumpToFirstMatchAndUpdateDir(
   LocationStack* stack,
   Boole forceit,
   OUT Boole* redrawForDummy,
   OUT Book* firstMatchBook,
   CS target_dir
){
   Book* book = curBook;
   llJump(stack, 0, 0, forceit);
   if (book != curBook)
      //If we jumped to another book redrawing will already be taken care of.
      *redrawForDummy = false;

   //Jump to the directory used after loading the book.
   if (curBook == firstMatchBook && target_dir) {
      Invocation invo;
      CLEAR_FIELD(invo);
      invo.arg = target_dir;
      invo.id = C_lcd;
      c_cd(&invo);
   }
}

//Process :vimgrep command arguments. The command syntax is:
//
// :{count}vimgrep /{pattern}/[g][j] {file} ...
private int
vimgrepProcessArgs(Invocation* invo, OUT VimGrepArgs* args) {
   CLEAR_POINTER(args);

   args->regmatch.regprog = NULL;
   args->title = copyStr(copyCommandTitle(*invo->commline));

   if (invo->addr_count > 0)
      args->tomatch = invo->line2;
   else
      args->tomatch = MAXLNUM;

   //Get the search pattern: either white-separated or enclosed in //
   CS p = skipEeglGrepPat(invo->arg, &args->spat, &args->flags);
   if (!p) {
      emsg(_(e_invalid_search_pattern_or_delimiter));
      return FAIL;
   }

   vgr_init_regmatch(&args->regmatch, args->spat);
   if (args->regmatch.regprog == NULL)
      return FAIL;

   p = skipwhite(p);
   if (*p == ZERO) {
      emsg(_(e_file_name_missing_or_invalid_pattern));
      return FAIL;
   }
   ExpandMatch matches = (ExpandMatch){.c = args->fnames, .len = args->fcount };

   //Parse the list of arguments, wildcards have already been expanded.
   if ((bookParseAndExpandFnames(p, true, OUT &matches) == FAIL) || args->fcount == 0) {
      emsg(_(e_no_match));
      return FAIL;
   }

   return OK;
}

//Search for a pattern in a list of files and populate the location list with the matches
private int
elckGrepFiles(
   LocationStack* stack,
   VimGrepArgs* invos,
   OUT Boole* redrawForDummy,
   OUT Book** firstMatchBook,
   OUT CS* target_dir
) {
   int status = FAIL;
   Unt idSave = getCurrent(stack)->id;
   int duplicate_name = false;

   Byte dirnameStart[MAXPATHL];
   Byte dirnameNow[MAXPATHL];
   //Remember the current directory, because a BufRead autocommand that does
   //":lcd %:p:h" changes the meaning of short path names.
   mch_dirname(dirnameStart, MAXPATHL);

   Tyme seconds = (Tyme)0;
   for (int fi = 0; fi < invos->fcount && !gotInterruptG && invos->tomatch > 0; ++fi) {
      CS fname = shorten_fname1(invos->fnames[fi]);
      if (time(NULL) > seconds) {
         //Display the file name every second or so, show the user we are working on it.
         seconds = time(NULL);
         vgr_display_fname(fname);
      }

      Book* book = booklistFindByNameExpandingLinks(invos->fnames[fi]);
      int using_dummy;
      if (!book || bookNoMemfile(book)) {
         //Remember that a book with this name already exists.
         duplicate_name = (book != NULL);
         using_dummy = true;
         *redrawForDummy = true;
         book = vgr_load_dummy_book(fname, dirnameStart, dirnameNow);
      } else
         //Use existing, loaded book.
         using_dummy = false;

      //Check whether the location list is still valid. When loading a
      //book above, autocommands might have changed the location list.
      if (!vgr_isIdValid(stack, idSave, invos->title))
         goto theend;

      idSave = getCurrent(stack)->id;

      if (book == NULL) {
         if (!gotInterruptG)
            smsg(_("Cannot open file \"%s\""), fname);
      } else {
         //Try for a match in all lines of the book.
         //For ":1vimgrep" look for first match only.
         int found_match = vgr_match_buflines(getCurrent(stack),
             fname, book, invos->spat, &invos->regmatch,
             &invos->tomatch, duplicate_name, invos->flags);

         if (using_dummy) {
            if (found_match && *firstMatchBook == NULL)
               *firstMatchBook = book;
            if (duplicate_name) {
               //Never keep a dummy buffer if there is another book with the same name.
               wipeDummyBook(book, dirnameStart);
               book = NULL;
            } ei ((commModifierG.cmod_flags & CMOD_HIDE) == 0){
               //When no match was found we don't need to remember the book, wipe it out. If 
               //there was a match and it wasn't the first one or we won't jump there: only unload
               //the book. Ignore 'hidden' here, because it may lead to having too many swap files
               if (!found_match) {
                  wipeDummyBook(book, dirnameStart);
                  book = NULL;
               } ei (book != *firstMatchBook
                     || (invos->flags & VGR_NOJUMP) != 0
                     || !bookNoFname(book)
               ) {
                  unloadDummyBook(book, dirnameStart);
                  //Keeping the book, remove the dummy flag.
                  book->flags &= ~BF_DUMMY;
                  book = NULL;
               }
            }

            if (book) {
               //Keeping the buffer, remove the dummy flag.
               book->flags &= ~BF_DUMMY;

               //If the buffer is still loaded we need to use the directory we jumped to below.
               if (book == *firstMatchBook
                      && *target_dir == NULL
                      && STRCMP(dirnameStart, dirnameNow) != 0)
                  *target_dir = copyStr(dirnameNow);

               //The book is still loaded, the Filetype autocommands need to be done now, in 
               //that book. need to be done (again). But not the portal-local options!
               AutocommSave   aco;
               auCommPrepareBook(&aco, book);
               if (curBook == book) {
                  applyAutocomms(EVENT_FILETYPE, book->fileType, book->currFileName, true, book);
                  auCommRestoreBook(&aco);
               }
            }
         }
      }
    }

    status = OK;

theend:
    return status;
}

//":vimgrep {pattern} file(s)". ":vimgrepadd {pattern} file(s)"
pub void
c_vimgrep(Invocation* invo) {
   if (!portCheckCanSetCurBookForceIt(invo->forceit))
      return;
      
   Boole redrawForDummy = false;
   Book* firstMatchBook = NULL;
   CS target_dir = NULL;

   CS auName = vgr_get_auname(invo->id);
   if (auName
         && applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()
   )
      return;

   LocationStack* stack = locationStacksP + LOC_LIST_GREP;

   VimGrepArgs args;
   if (vimgrepProcessArgs(invo, OUT &args) == FAIL)
      goto theend;

   if ((invo->id != C_grepadd && invo->id != C_vimgrepadd) || isStackEmpty(stack)) {
      //make place for a new list
      newLocList(stack, args.title);
   } 

   incrementLlBusyness();

   int status = elckGrepFiles(stack, &args, OUT &redrawForDummy, OUT &firstMatchBook, OUT &target_dir);
   
   ExpandMatch matches = (ExpandMatch){.c = args.fnames, .len = args.fcount, .a = createArena() };
   if (status != OK) {
      decrementLlBusyness();
      goto theend;
   }

   LocationList* ll = getCurrent(stack);
   ll->noValidEntries = false;
   ll->curr = ll->first;
   ll->currentIdx = 1;
   updateChangedTick(ll);

   updateBook(stack, NULL);

   //Remember the current location list identifier, so that we can check for
   //autocommands changing the current location list.
   Unt idSave = getCurrent(stack)->id;

   if (auName)
      applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
   //The QuickFixCmdPost autocmd may free the location list. Check the list
   //is still valid.
   if (!isIdValid(stack, idSave) || restoreList(stack, idSave) == FAIL) {
      decrementLlBusyness();
      goto theend;
   }

   //Jump to first match.
   if (!isEmpty(getCurrent(stack))) {
      if ((args.flags & VGR_NOJUMP) == 0)
         jumpToFirstMatchAndUpdateDir(
               stack, invo->forceit, OUT &redrawForDummy, OUT firstMatchBook, target_dir
         );
   } else
      showErrFmtMsg(_(e_no_match_str_2), args.spat);

   decrementLlBusyness();

   //If we loaded a dummy buffer into the current portal, the autocommands
   //may have messed up things, need to redraw and recompute folds.
   if (redrawForDummy) {
      foldUpdateAll(curPor);
   }

theend:
   deleteArena(matches.a);
   eeglFree(args.title);
   eeglFree(target_dir);
   eeRegFree(args.regmatch.regprog);
}

//Restore current working directory to "dirname_start" if they differ, taking
//into account whether it is set locally or globally.
private void
restore_start_dir(CS dirname_start) {
   Byte dirname_now[MAXPATHL];
   mch_dirname(dirname_now, MAXPATHL);
   if (STRCMP(dirname_start, dirname_now) != 0) {
      //If the directory has changed, change it back by building up an
      //appropriate command and executing it.
      Invocation invo;

      CLEAR_FIELD(invo);
      invo.arg = dirname_start;
      invo.id = (curPor->localDir == NULL) ? C_cd : C_lcd;
      c_cd(&invo);
   }
}

//Load file "fname" into a dummy book and return the book pointer,
//placing the directory resulting from the book load into the
//"resulting_dir" pointer. "resulting_dir" must be allocated by the caller
//prior to calling this function. Restores directory to "dirname_start" prior
//to returning, if autocmds or the 'autochdir' option have changed it.
//
//If creating the dummy book does not fail, must call unloadDummyBook()
//or wipeDummyBook() later!
//
//Return NULL if it fails.
private Book*
loadDummyBook(
   CS fname,
   CS dirname_start,  //in: old directory
   CS resulting_dir  //out: new directory
){
   BookRef   newbufref;
   BookRef   newbuf_to_wipe;
   int      failed = true;
   AutocommSave   aco;
   int      readfile_result;

   //Allocate a book without putting it in the book list.
   Book* newBook = bookNew(NULL, NULL, (LineNr)1, BLN_DUMMY);
   if (!newBook)
      return NULL;
   bookStoreInRef(OUT &newbufref, newBook);

   //Init the options.
   optsCopyToBook(newBook, BCO_ENTER);

   //need to open the memfile before opening a portal into the book
   if (ml_open(newBook) == OK) {
      //Make sure this book isn't wiped out by autocommands.
      ++newBook->locked;

      //set curPor/curBook to book and save a few things
      auCommPrepareBook(&aco, newBook);
      if (curBook == newBook) {
         //Need to set the filename for autocommands.
         (void)setfname(curBook, fname, NULL, false);

         //Create swap file now to avoid the ATTENTION message.
         check_need_swap(true);

         //Remove the "dummy" flag, otherwise autocommands may not work.
         curBook->flags &= ~BF_DUMMY;

         newbuf_to_wipe.c = NULL;
         readfile_result = readfile(
            fname, NULL, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, NULL, READ_NEW | READ_DUMMY
         );
         --newBook->locked;
         if (readfile_result == OK && !gotInterruptG && !(curBook->flags & BF_NEW)) {
            failed = false;
            if (curBook != newBook) {
                //Bloody autocommands changed the book!  Can happen when
                //using netrw and editing a remote file.  Use the current
                //book instead, delete the dummy one after restoring the portal stuff.
                bookStoreInRef(OUT &newbuf_to_wipe, newBook);
                newBook = curBook;
            }
         }

         //restore curPor/curBook and a few other things
         auCommRestoreBook(&aco);

         if (newbuf_to_wipe.c != NULL && bookRefValid(&newbuf_to_wipe)) {
            block_autocmds();
            wipeDummyBook(newbuf_to_wipe.c, NULL);
            unblock_autocmds();
         }
      }

      //Add back the "dummy" flag, otherwise booklistFindName_stat() won't skip it.
      newBook->flags |= BF_DUMMY;
   }

   //When autocommands/'autochdir' option changed directory: go back.
   //Let the caller know that the resulting dir was first, in case it is important.
   mch_dirname(resulting_dir, MAXPATHL);
   restore_start_dir(dirname_start);

   if (!bookRefValid(&newbufref))
      return NULL;
   if (failed) {
      wipeDummyBook(newBook, dirname_start);
      return NULL;
   }
   return newBook;
}

//Wipe out the dummy book that loadDummyBook() created. Restores
//directory to "dirname_start" if not NULL prior to returning, if autocmds or
//the 'autochdir' option have changed it.
private void
wipeDummyBook(Book* book, CS dirname_start) {
   //If any autocommand opened a portal into the dummy book, close that portal.  
   //If we can't close them all then give up.
   while (book->countPortals > 0) {
      int       did_one = false;
      Portal       *wp;

      if (firstPor->next != NULL)
         FOR_ALL_PORTALS(wp)
         if (wp->book == book) {
             if (closePortal(wp, false) == OK)
            did_one = true;
             break;
         }
      if (!did_one)
          goto fail;
   }

   if (curBook != book && book->countPortals == 0) {  //safety check
      Cleanup   cs;

      //Reset the error/interrupt/exception state here so that aborting()
      //returns false when wiping out the book.  Otherwise it doesn't
      //work when gotInterruptG is set.
      enter_cleanup(&cs);

      bookWipe(book, true);

      //Restore the error/interrupt/exception state if not discarded by a
      //new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
      if (dirname_start != NULL)
          //When autocommands/'autochdir' option changed directory: go back.
          restore_start_dir(dirname_start);

      return;
    }

fail:
    //Keeping the book, remove the dummy flag.
    book->flags &= ~BF_DUMMY;
}

//Unload the dummy book that loadDummyBook() created. Restores
//directory to "dirname_start" prior to returning, if autocmds or the
//'autochdir' option have changed it.
private void
unloadDummyBook(Book* book, CS dirname_start) {
   if (curBook == book)      //safety check
      return;

   bookClose(NULL, book, DOBOOK_UNLOAD, false, true);

   //When autocommands/'autochdir' option changed directory: go back.
   restore_start_dir(dirname_start);
}

//Copy the specified location entry items into a new bag and append the bag
//to 'list'.  Returns OK on success.
private int
get_qfline_items(LocLine *lline, List *list) {
   //Handle entries with a non-existing book number.
   int bufnum = lline->fNum;
   if (bufnum != 0 && (bookFindFileByBookNr(bufnum) == NULL))
      bufnum = 0;

   Bag* bag = allocBag();
   if (listAppendBag(list, bag) == FAIL)
      return FAIL;

   Byte buf[2];
   buf[0] = lline->kind;
   buf[1] = ZERO;
   return (bagAddNumber(bag, S"bufnr", (long)bufnum) == FAIL
          || bagAddNumber(bag, S"lnum",     (long)lline->lNum) == FAIL
          || bagAddNumber(bag, S"end_lnum", (long)lline->endLNum) == FAIL
          || bagAddNumber(bag, S"col",      (long)lline->col) == FAIL
          || bagAddNumber(bag, S"end_col",  (long)lline->endCol) == FAIL
          || bagAddNumber(bag, S"vcol",     (long)lline->visCol) == FAIL
          || bagAddNumber(bag, S"nr",       (long)lline->errNum) == FAIL
          || bagAddString(bag, S"module", lline->moduleName) == FAIL
          || bagAddString(bag, S"pattern", lline->pattern) == FAIL
          || bagAddString(bag, S"text", lline->text) == FAIL
          || bagAddString(bag, S"type", buf) == FAIL
          || (lline->userData.tag != VAR_UNKNOWN
               && bagAddVar(bag, S"user_data", &lline->userData) == FAIL )
          || bagAddNumber(bag, S"valid", (long)lline->isValid) == FAIL
   ) ? FAIL : OK;
}

//Add each item from a location list to the output list as a dictionary. If ind is -1, use the 
//current list. Otherwise, use the specified list. If entryId is not 0, then return only the 
//specified entry. Otherwise return all the entries.
private int
exportLocList(
   LocationStack* stack,
   Unt ind,
   int entryId,
   OUT List* list
){
   if (!stack)
      return FAIL;

   if (entryId < 0)
      return OK;

   if (ind == INVALID_LL_IND)
      ind = stack->currList;

   if (ind >= stack->listcount)
      return FAIL;

   LocationList* ll = getList(stack, ind);
   if (isEmpty(ll))
      return FAIL;

   LocLine* lline;
   int i;
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if (entryId > 0) {
         if (entryId == i)
            return get_qfline_items(lline, list);
      } ei (get_qfline_items(lline, list) == FAIL)
         return FAIL;
   }

   return OK;
}

//Flags used by getqflist()/getloclist() to determine which fields to return.
enum {
   QF_GETLIST_NONE    = 0x0,
   QF_GETLIST_TITLE   = 0x1,
   QF_GETLIST_ITEMS   = 0x2,
   QF_GETLIST_NR      = 0x4,
   QF_GETLIST_WINID   = 0x8,
   QF_GETLIST_CONTEXT = 0x10,
   QF_GETLIST_ID      = 0x20,
   QF_GETLIST_IDX     = 0x40,
   QF_GETLIST_SIZE    = 0x80,
   QF_GETLIST_TICK    = 0x100,
   QF_GETLIST_QFBUFNR = 0x200,
   QF_GETLIST_QFTF    = 0x400,
   QF_GETLIST_ALL     = 0x800
};

//Parse text from 'di' and return the location list items.
//Existing location lists are not modified.
private int
getList_from_lines(Bag* specifics, DictItem* di, OUT Bag* retBag) {
   //Only a List value is supported
   if (di->c.tag != VAR_LIST || di->c.list == NULL)
      return FAIL;

   int status = FAIL;
   CS errorformat = curBook->o.errorFormat;
   
   //If errorformat is supplied then use it, otherwise use the [errorformat] option
   DictItem* item;
   if ((item = bagFind(specifics, tConst("efm"))) != NULL) {
      if (item->c.tag != VAR_STRING || item->c.string == NULL)
         return FAIL;
      errorformat = item->c.string;
   }

   List* l = list_alloc();

   LocationStack* stack = ALLOC_CLEAR_ONE_ID(LocationStack, aid_ll_module);
	if (!stack)
      return FAIL;
   stack->refCount++;
   stack->bufNum = INVALID_LL_BUFNR;
   stack->lists = allocateLocList(STACK_CAPACITY);
   if (stack->lists == NULL) {
      return FAIL;
   }
   
   Source source = (Source){.tag = SOURCE_LIST, .List = (ListSource){.c = di->c.list->first}};
   if (initWorker(source, stack, 0, errorformat, true, NULL) > 0) {
      (void)exportLocList(stack, 0, 0, l);
      freeAList(&stack->lists[0]);
   }

   freeAList_lists(stack);
   bagAddList(retBag, S"items", l);
   status = OK;

   return status;
}

//Return the location list portal identifier in the current tab.
private int
getLlPortalId(LocationStack* stack) {
   //The location portal can be opened even if the location list is not set
   //using ":mopen". This is not true for location lists.
   if (!stack)
      return 0;
   Portal* po = findPortalIntoLocList(stack);
   return (po) ? po->id : 0;
}

//Return the number of the book displayed in the location list portal. If there is no book 
//associated with the list or the book is wiped out, then returns 0.
private int
qf_getprop_qfbufnr(LocationStack* stack, Bag* retBag) {
   int   bufnum = 0;

   if (stack && bookFindFileByBookNr(stack->bufNum) != NULL)
      bufnum = stack->bufNum;

   return bagAddNumber(retBag, S"qfbufnr", bufnum);
}

//Convert the keys in 'specifics' to location list property flags.
private Unt
importKeysFromDict(Bag* specifics) {
   Unt flags = QF_GETLIST_NONE;

   if (bagHasKey(specifics, tConst("all"))) {
      flags |= QF_GETLIST_ALL;
   }

   if (bagHasKey(specifics, tConst("title")))
      flags |= QF_GETLIST_TITLE;

   if (bagHasKey(specifics, tConst("nr")))
      flags |= QF_GETLIST_NR;

   if (bagHasKey(specifics, tConst("winid")))
      flags |= QF_GETLIST_WINID;

   if (bagHasKey(specifics, tConst("context")))
      flags |= QF_GETLIST_CONTEXT;

   if (bagHasKey(specifics, tConst("id")))
      flags |= QF_GETLIST_ID;

   if (bagHasKey(specifics, tConst("items")))
      flags |= QF_GETLIST_ITEMS;

   if (bagHasKey(specifics, tConst("idx")))
      flags |= QF_GETLIST_IDX;

   if (bagHasKey(specifics, tConst("size")))
      flags |= QF_GETLIST_SIZE;

   if (bagHasKey(specifics, tConst("changedtick")))
      flags |= QF_GETLIST_TICK;

   if (bagHasKey(specifics, tConst("qfbufnr")))
      flags |= QF_GETLIST_QFBUFNR;

   if (bagHasKey(specifics, tConst("quickfixtextfunc")))
      flags |= QF_GETLIST_QFTF;

   return flags;
}

//Return the location list index based on 'nr' or 'id' in 'specifics'.
//If 'nr' and 'id' are not present in 'specifics' then return the current location list index.
//If 'nr' is zero then return the current location list index.
//If 'nr' is '$' then return the last location list index.
//If 'id' is present then return the index of the location list with that id.
//If 'id' is zero then return the location list index specified by 'nr'.
//Return -1, if location list is not present or if the stack is empty.
private int
qf_getprop_qfidx(LocationStack* stack, Bag* specifics) {
   DictItem* di;

   Unt ind = stack->currList;   //default is the current list
   if ((di = bagFind(specifics, tConst("nr"))) != NULL) {
      //Use the specified location list
      if (di->c.tag == VAR_NUMBER) {
         //for zero use the current list
         if (di->c.number != 0) {
            ind = di->c.number - 1;
            if (ind >= stack->listcount)
               ind = INVALID_LL_IND;
         }
      } ei (di->c.tag == VAR_STRING
         && di->c.string != NULL
         && STRCMP(di->c.string, "$") == 0)
          //Get the last location list number
          ind = stack->listcount - 1;
      else
          ind = INVALID_LL_IND;
    }

   if ((di = bagFind(specifics, tConst("id"))) != NULL) {
      //Look for a list with the specified id
      if (di->c.tag == VAR_NUMBER) {
         //For zero, use the current list or the list specified by 'nr'
         if (di->c.number != 0)
            ind = idToNr(stack, di->c.number);
      } else
         ind = INVALID_LL_IND;
   }

   return ind;
}

//Return default values for location list properties in retBag.
private int
getPropertyDefaults(LocationStack* stack, Unt flags, OUT Bag* retBag) {
   int      status = OK;

   if (flags & QF_GETLIST_TITLE)
      status = bagAddString(retBag, S"title", (CS)"");
   if ((status == OK) && (flags & QF_GETLIST_ITEMS)) {
      List* l = list_alloc();
      status = bagAddList(retBag, S"items", l);
   }
   if ((status == OK) && (flags & QF_GETLIST_NR))
      status = bagAddNumber(retBag, S"nr", 0);
   if ((status == OK) && (flags & QF_GETLIST_WINID))
      status = bagAddNumber(retBag, S"winid", getLlPortalId(stack));
   if ((status == OK) && (flags & QF_GETLIST_CONTEXT))
      status = bagAddString(retBag, S"context", (CS)"");
   if ((status == OK) && (flags & QF_GETLIST_ID))
      status = bagAddNumber(retBag, S"id", 0);
   if ((status == OK) && (flags & QF_GETLIST_IDX))
      status = bagAddNumber(retBag, S"idx", 0);
   if ((status == OK) && (flags & QF_GETLIST_SIZE))
      status = bagAddNumber(retBag, S"size", 0);
   if ((status == OK) && (flags & QF_GETLIST_TICK))
      status = bagAddNumber(retBag, S"changedtick", 0);
   if ((status == OK) && (flags & QF_GETLIST_QFBUFNR))
      status = qf_getprop_qfbufnr(stack, retBag);
   if ((status == OK) && (flags & QF_GETLIST_QFTF))
      status = bagAddString(retBag, S"quickfixtextfunc", S"");

   return status;
}

//Return the location list title as 'title' in retBag
private int
qf_getprop_title(LocationList* ll, Bag* retBag) {
   return bagAddString(retBag, S"title", ll->title);
}

//Return the location list items/entries as 'items' in retBag.
//If eidx is not 0, then return the item at the specified index.
private int
exportToDict(LocationStack* stack, Unt ind, int eidx, OUT Bag* retBag) {
   int      status = OK;
   List   *l = list_alloc();
   (void)exportLocList(stack, ind, eidx, l);
   bagAddList(retBag, S"items", l);

   return status;
}

//Return the location list context (if any) as 'context' in retBag.
private int
exportContext(LocationList* ll, OUT Bag* retBag) {
   int status;

   if (ll->qf_ctx != NULL) {
      DictItem* di = dictitem_alloc(tConst("context"));
      copy_tv(OUT &di->c, ll->qf_ctx);
      status = bagAdd(retBag, di);
      if (status == FAIL)
         dictitem_free(di);
   } else
      status = bagAddString(retBag, S"context", (CS)"");

   return status;
}

//Return the current location list index as 'idx' in retBag.
//If a specific entry index (eidx) is supplied, then use that.
private int
qf_getprop_idx(LocationList* ll, int eidx, Bag* retBag) {
   if (eidx == 0) {
      eidx = ll->currentIdx;
      if (isEmpty(ll))
         //For empty lists, current index is set to 0
         eidx = 0;
   }
   return bagAddNumber(retBag, S"idx", eidx);
}

//Return the 'quickfixtextfunc' function of a location list
private int
qf_getprop_qftf(LocationList* ll, Bag* retBag) {
   int status;
   if (ll->textFn.name) {
      Var   tv;
      putCallback(OUT &tv, &ll->textFn);
      status = bagAddVar(retBag, S"quickfixtextfunc", &tv);
      clearVar(&tv);
   } else
      status = bagAddString(retBag, S"quickfixtextfunc", (CS)"");

   return status;
}

//Return location list details (title) as a dictionary. 'specifics' contains the details to 
//return. If 'list_idx' is -1, then current list is used. Otherwise the specified list is used.
private int
getProperties(LocationStack* stack, Bag* specifics, OUT Bag* retBag) {
   int status = OK;
   Unt ind = INVALID_LL_IND;
   int eidx = 0;
   DictItem* di;

   if ((di = bagFind(specifics, tConst("lines"))) != NULL)
      return getList_from_lines(specifics, di, OUT retBag);


   Unt flags = importKeysFromDict(specifics);

   if (!isStackEmpty(stack))
      ind = qf_getprop_qfidx(stack, specifics);

   //List is not present or is empty
   if (isStackEmpty(stack) || ind == INVALID_LL_IND)
      return getPropertyDefaults(stack, flags, retBag);

   LocationList* ll = getList(stack, ind);

    //If an entry index is specified, use that
   if ((di = bagFind(specifics, tConst("idx"))) != NULL) {
      if (di->c.tag != VAR_NUMBER)
         return FAIL;
      eidx = di->c.number;
   }

   if (flags & QF_GETLIST_TITLE)
      status = qf_getprop_title(ll, retBag);
   if ((status == OK) && (flags & QF_GETLIST_NR))
      status = bagAddNumber(retBag, S"nr", ind + 1);
   if ((status == OK) && (flags & QF_GETLIST_WINID))
      status = bagAddNumber(retBag, S"winid", getLlPortalId(stack));
   if ((status == OK) && (flags & QF_GETLIST_ITEMS))
      status = exportToDict(stack, ind, eidx, retBag);
   if ((status == OK) && (flags & QF_GETLIST_CONTEXT))
      status = exportContext(ll, retBag);
   if ((status == OK) && (flags & QF_GETLIST_ID))
      status = bagAddNumber(retBag, S"id", ll->id);
   if ((status == OK) && (flags & QF_GETLIST_IDX))
      status = qf_getprop_idx(ll, eidx, retBag);
   if ((status == OK) && (flags & QF_GETLIST_SIZE))
      status = bagAddNumber(retBag, S"size", ll->count);
   if ((status == OK) && (flags & QF_GETLIST_TICK))
      status = bagAddNumber(retBag, S"changedtick", ll->changedTick);
   if ((status == OK) && (flags & QF_GETLIST_QFBUFNR))
      status = qf_getprop_qfbufnr(stack, retBag);
   if ((status == OK) && (flags & QF_GETLIST_QFTF))
      status = qf_getprop_qftf(ll, retBag);

   return status;
}

//Add a new location entry to list at 'ind' in the stack 'stack' from the
//items in the dict 'd'. If it is a valid error entry, then set 'valid_entry' to true.
private int
addEntry_from_dict(LocationList* ll, Bag* d, int first_entry, int* valid_entry){
   static int   did_bufnr_emsg;

   if (first_entry)
      did_bufnr_emsg = false;

   CS filename = bagGetString(d,tConst("filename"), true);
   CS module = bagGetString(d,tConst("module"), true);
   int bufnum = (int)bagGetNumber(d, tConst("bufnr"));
   long lnum = (int)bagGetNumber(d, tConst("lnum"));
   long end_lnum = (int)bagGetNumber(d, tConst("end_lnum"));
   int col = (int)bagGetNumber(d, tConst("col"));
   int end_col = (int)bagGetNumber(d, tConst("end_col"));
   int vcol = (int)bagGetNumber(d, tConst("vcol"));
   int nr = (int)bagGetNumber(d, tConst("nr"));
   CS type = bagGetString(d, tConst("type"), true);
   CS pattern = bagGetString(d, tConst("pattern"), true);
   CS text = bagGetString(d, tConst("text"), true);
   if (!text)
      text = copyStr(S"");
   Var user_data;
   user_data.tag = VAR_UNKNOWN;
   bagGetVar(d, tConst("user_data"), &user_data);

   Boole valid = true;
   if ((filename == NULL && bufnum == 0) || (lnum == 0 && pattern == NULL))
      valid = false;

   //Mark entries with non-existing book number as not valid. Give the error message only once.
   if (bufnum != 0 && (bookFindFileByBookNr(bufnum) == NULL)) {
      if (!did_bufnr_emsg) {
         did_bufnr_emsg = true;
         showErrFmtMsg(_(e_book_nr_not_found), bufnum);
      }
      valid = false;
      bufnum = 0;
   }

   //If the 'valid' field is present it overrules the detected value.
   if (bagHasKey(d, tConst("valid")))
      valid = bagGetBool(d, tConst("valid"), false);

   int status = addEntry(ll,
        NULL,      //dir
        filename,
        module,
        bufnum,
        text,
        lnum,
        end_lnum,
        col,
        end_col,
        vcol,      //vis_col
        pattern,   //search pattern
        nr,
        type == NULL ? ZERO : *type,
        &user_data,
        valid
   );

   eeglFree(filename);
   eeglFree(module);
   eeglFree(pattern);
   eeglFree(text);
   eeglFree(type);
   clearVar(&user_data);

   if (valid)
      *valid_entry = true;

   return status;
}

//Check if `entry` is closer to the target than `other_entry`.
//
//Only return true if `entry` is definitively closer. If it's further away, or there's not 
//enough information to tell, return false.
private int
entry_is_closer_to_target(
   LocLine* entry,
   LocLine* other_entry,
   int target_fnum,
   int target_lnum,
   int target_col
) {
   //First, compare entries to target file.
   if (!target_fnum)
      //Without a target file, we can't know which is closer.
      return false;

   int is_target_file = entry->fNum && entry->fNum == target_fnum;
   int other_is_target_file = other_entry->fNum && other_entry->fNum == target_fnum;
   if (!is_target_file && other_is_target_file)
      return false;
   ei (is_target_file && !other_is_target_file)
      return true;

    //Both entries are pointing at the exact same file. Now compare line
    //numbers.
   if (!target_lnum)
      //Without a target line number, we can't know which is closer.
      return false;

   int line_distance = entry->lNum ? labs(entry->lNum - target_lnum) : INT_MAX;
   int other_line_distance = other_entry->lNum ? labs(other_entry->lNum - target_lnum) : INT_MAX;
   if (line_distance > other_line_distance)
      return false;
   ei (line_distance < other_line_distance)
      return true;

   //Both entries are pointing at the exact same line number (or no line
   //number at all). Now compare columns.
   if (!target_col)
      //Without a target column, we can't know which is closer.
      return false;

   int column_distance = entry->col ? abs(entry->col - target_col) : INT_MAX;
   int other_column_distance = other_entry->col ? abs(other_entry->col - target_col): INT_MAX;
   if (column_distance > other_column_distance)
      return false;
   ei (column_distance < other_column_distance)
      return true;

   //It's a complete tie! The exact same file, line, and column.
   return false;
}

//Add list of entries to location list. Each list entry is a dictionary with item information.
private int
addEntries(
   OUT LocationStack* stack,
   Unt ind,
   List* list,
   CS title,
   LocListAction action
){
   Bag   *d;
   LocLine   *oldLast = NULL;
   int      retval = OK;
   int      valid_entry = false;

   //If there's an entry selected in the location list, remember its location
   //(file, line, column), so we can select the nearest entry in the updated list.
   int prev_fnum = 0;
   int prev_lnum = 0;
   int prev_col = 0;
   LocationList* ll = getList(stack, ind);
   if (ll->curr) {
      prev_fnum = ll->curr->fNum;
      prev_lnum = ll->curr->lNum;
      prev_col = ll->curr->col;
   }

   int select_first_entry = false;
   int select_nearest_entry = false;

   if (action == LL_ACTION_NEW || ind == stack->listcount) {
      select_first_entry = true;
      //make place for a new list
      newLocList(stack, title);
      ind = stack->currList;
      ll = getList(stack, ind);
   } ei (action == LL_ACTION_ADD) {
      if (isEmpty(ll))
         //Appending to empty list, select first entry.
         select_first_entry = true;
      else
         //Adding to existing list, use last entry.
         oldLast = ll->last;
   } ei (action == LL_ACTION_REPLACE) {
      select_first_entry = true;
      freeItems(ll);
      storeTitle(ll, title);
   } ei (action == LL_ACTION_UPDATE) {
      select_nearest_entry = true;
      freeItems(ll);
      storeTitle(ll, title);
   }

   LocLine *entry_to_select = NULL;
   int entry_to_select_index = 0;

   ListItem* li;
   FOR_ALL_LIST_ITEMS(list, li) {
      if (li->c.tag != VAR_BAG)
         continue; //Skip non-dict items

      d = li->c.bag;
      if (!d)
         continue;

      retval = addEntry_from_dict(ll, d, li == list->first, &valid_entry);
      if (retval == QF_FAIL)
         break;

      LocLine *entry = ll->last;
      if ((select_first_entry && entry_to_select == NULL)
          || (select_nearest_entry &&
               (entry_to_select == NULL
                  || entry_is_closer_to_target(
                        entry, entry_to_select, prev_fnum, prev_lnum, prev_col)
               )
             )
      ){
         entry_to_select = entry;
         entry_to_select_index = ll->count;
      }
   }

   //Check if any valid error entries are added to the list.
   if (valid_entry)
      ll->noValidEntries = false;
   ei (ll->currentIdx == 0)
      //no valid entry
      ll->noValidEntries = true;

   //Set the current error.
   if (entry_to_select) {
      ll->curr = entry_to_select;
      ll->currentIdx = entry_to_select_index;
   }

   //Don't update the cursor in location portal when appending entries
   updateBook(stack, oldLast);

   return retval;
}

//Get the location list index from 'nr' or 'id'
private Unt
qf_setprop_get_qfidx(
   LocationStack* stack,
   Bag* specifics,
   LocListAction action,
   OUT Boole* newlist
){
   DictItem   *di;
   Unt ind = stack->currList;    //default is the current list

   if ((di = bagFind(specifics, tConst("nr"))) != NULL) {
      //Use the specified location list
      if (di->c.tag == VAR_NUMBER) {
         //for zero use the current list
         if (di->c.number != 0)
            ind = di->c.number - 1;

         if ((action == LL_ACTION_ADD) && ind == stack->listcount) {
            //When creating a new list, accept ind pointing to the next
            //non-available list and add the new list at the end of the stack.
            *newlist = true;
            ind = isStackEmpty(stack) ? 0 : stack->listcount - 1;
         } ei (ind >= stack->listcount)
            return INVALID_LL_IND;
         else
            *newlist = false;   //use the specified list
      } ei (di->c.tag == VAR_STRING
            && di->c.string != NULL
            && STRCMP(di->c.string, "$") == 0) {
         if (!isStackEmpty(stack))
            ind = stack->listcount - 1;
         ei (*newlist)
            ind = 0;
         else
            return INVALID_LL_IND;
      } else
          return INVALID_LL_IND;
   }

   if (!*newlist && (di = bagFind(specifics, tConst("id"))) != NULL) {
      //Use the location list with the specified id
      if (di->c.tag != VAR_NUMBER)
         return INVALID_LL_IND;

      return idToNr(stack, di->c.number);
   }

   return ind;
}

private int
setTitle(LocationStack* stack, Unt ind, Bag* specifics, DictItem* di) {
   LocationList* ll = getList(stack, ind);

   if (di->c.tag != VAR_STRING)
      return FAIL;

   eeglFree(ll->title);
   ll->title = bagGetString(specifics, tConst("title"), true);
   if (ind == stack->currList)
      updateTitleVar(stack);

   return OK;
}

//Set location list items/entries.
private int
setItems(LocationStack* stack, Unt ind, DictItem* di, LocListAction action) {
   if (di->c.tag != VAR_LIST)
      return FAIL;

   CS title_save = copyStr(stack->lists[ind].title);
   int retval = addEntries(stack, ind, di->c.list, title_save, action);
   eeglFree(title_save);

   return retval;
}

//Set location list entries from a list of lines.
private int
setLinesFromList(
   LocationStack* stack,
   Unt ind,
   Bag* specifics,
   DictItem* di,
   LocListAction action
){
   CS errorformat = curBook->o.errorFormat;
   DictItem* efm_di;
   int retval = FAIL;

   //Use the user supplied errorformat settings (if present)
   if ((efm_di = bagFind(specifics, tConst("efm"))) != NULL) {
      if (efm_di->c.tag != VAR_STRING || efm_di->c.string == NULL)
         return FAIL;
      errorformat = efm_di->c.string;
   }

   //Only a List value is supported
   if (di->c.tag != VAR_LIST || di->c.list == NULL)
      return FAIL;

   if (action == LL_ACTION_REPLACE || action == LL_ACTION_UPDATE)
      freeItems(&stack->lists[ind]);
   Source source = (Source){ .tag = SOURCE_LIST, .List = (ListSource){.c = di->c.list->first} };
   if (initWorker(source, stack, ind, errorformat, false, NULL) >= 0)
      retval = OK;

   return retval;
}

//Set location list context.
private int
setContext(LocationList* ll, DictItem* di) {
   freeVar(ll->qf_ctx);
   Var* ctx =  allocVar();
   if (ctx)
      copy_tv(OUT ctx, &di->c);
   ll->qf_ctx = ctx;

   return OK;
}

//Set the current index in the specified location list
private int
setCurrentIndex(LocationStack *stack, LocationList *ll, DictItem *di){
   Boole denote = false;
   int  old_qfidx;
   LocLine   *curr;

   //If the specified index is '$', then use the last entry
   int newidx;
   if (di->c.tag == VAR_STRING && di->c.string && STRCMP(di->c.string, "$") == 0) {
      newidx = ll->count;
   } else {
      //Otherwise use the specified index
      newidx = varGetNumberChk(&di->c, OUT &denote);
      if (denote)
         return FAIL;
   }

   if (newidx < 1)      //sanity check
      return FAIL;
   if (newidx > ll->count)
      newidx = ll->count;

   old_qfidx = ll->currentIdx;
   curr = getNthEntry(ll, newidx, &newidx);
   if (!curr)
      return FAIL;
   ll->curr = curr;
   ll->currentIdx = newidx;

   //If the current list is modified and a location portal into it is open, then Update it
   if (getCurrent(stack)->id == ll->id)
      (void)updatePortalPos(stack, old_qfidx);

   return OK;
}

//Set the current callback in the specified location list
private int
setTextFn(LocationList *ll, DictItem *di) {
   evFreeCallback(&ll->textFn);
   Callback callback = get_callback(&di->c);
   if (!callback.name || *callback.name == ZERO)
      return OK;

   set_callback(&ll->textFn, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);

   return OK;
}

//Set location list properties (title, items, context). Also used to add items from parsing a list
//of lines. Used by the setqflist() and setloclist() Vim script functions.
private int
setProperties(LocationStack *stack, Bag *specifics, LocListAction action, CS title) {
   int      retval = FAIL;
   Boole newlist = (action == LL_ACTION_NEW || isStackEmpty(stack));

   Unt ind = qf_setprop_get_qfidx(stack, specifics, action, OUT &newlist);
   if (ind == INVALID_LL_IND)   //List not found
      return FAIL;

   if (newlist) {
      stack->currList = ind;
      newLocList(stack, title);
      ind = stack->currList;
   }

   LocationList* ll = getList(stack, ind);
   DictItem   *di;
   if ((di = bagFind(specifics, tConst("title"))) != NULL)
      retval = setTitle(stack, ind, specifics, di);
   if ((di = bagFind(specifics, tConst("items"))) != NULL)
      retval = setItems(stack, ind, di, action);
   if ((di = bagFind(specifics, tConst("lines"))) != NULL)
      retval = setLinesFromList(stack, ind, specifics, di, action);
   if ((di = bagFind(specifics, tConst("context"))) != NULL)
      retval = setContext(ll, di);
   if ((di = bagFind(specifics, tConst("idx"))) != NULL)
      retval = setCurrentIndex(stack, ll, di);
   if ((di = bagFind(specifics, tConst("quickfixtextfunc"))) != NULL)
      retval = setTextFn(ll, di);

   if (newlist || retval == OK)
      updateChangedTick(ll);
   if (newlist)
      updateBook(stack, NULL);

   return retval;
}

//Free an entire location list stack. If there is a portal into it, then clear it.
private void
freeTheStack(LocationStack* stack) {
   Portal* mbLocPortal = findPortalIntoLocList(stack);
   
   if (mbLocPortal) {
      //If the location list portal is open, then clear it
      if (stack->currList < stack->listcount)
          freeAList(getCurrent(stack));
      updateBook(stack, NULL);
   }
   freeAList_list_stack_items(stack);
}

//Populate the location list with the items supplied in the list
//of dictionaries. "title" will be copied to w:quickfix_title.
//Otherwise create a new list. When "specifics" is not NULL then only set some properties.
pub int
setLocationList(
   OUT LocationStack* stack,
   List* newContent,
   LocListAction action,
   CS title,
   Bag* specific
) {
   if (stack == NULL)
      return FAIL;

   int retval = OK;
   if (action == LL_ACTION_FREE) {
      //Free the entire location list stack
      freeTheStack(stack);
      return OK;
   }

   //A dict argument cannot be specified with a non-empty list argument
   if (newContent->len != 0 && specific != NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), _("cannot have both a list and a \"specific\" argument"));
      return FAIL;
   }

   incrementLlBusyness();

   if (!specific)
      retval = addEntries(stack, stack->currList, newContent, title, action);
   if (retval == OK)
      updateChangedTick(getCurrent(stack));
   else {
      retval = setProperties(stack, specific, action, title);
   }

   decrementLlBusyness();

   return retval;
}

private Boole
checkIfUserDataLocked(LocationStack* stack, int copyID) {
   Boole abort = false;
   for (Unt i = 0; i < stack->cap && !abort; ++i) {
      LocationList *ll = &stack->lists[i];
      if (!ll->hasUserData)
         continue;
      LocLine *lline;
      int j;
      FOR_ALL_LL_ITEMS(ll, lline, j) {
         Var* user_data = &lline->userData;
         if (user_data != NULL && user_data->tag != VAR_NUMBER
               && user_data->tag != VAR_STRING && user_data->tag != VAR_FLOAT
         ) {
            abort = abort || set_ref_in_item(user_data, copyID, NULL, NULL);
         } 
      }
   }
   return abort;
}

//Check the location context and callback function if they are in use. For all the lists
//in a location stack.
private Boole
checkIfContextAndCallbackLocked(LocationStack* stack, int copyID) {
   Boole abort = false;

   for (Unt i = 0; i < stack->cap && !abort; ++i) {
      Var* ctx = stack->lists[i].qf_ctx;
      if (ctx != NULL && ctx->tag != VAR_NUMBER
            && ctx->tag != VAR_STRING && ctx->tag != VAR_FLOAT) {
         abort = abort || set_ref_in_item(ctx, copyID, NULL, NULL);
      } 

      Callback* cb = &stack->lists[i].textFn;
      abort = abort || memSetRefInCallback(cb, copyID);
   }

   return abort;
}

private Boole
markReferencesInStack(LocationStack* st, int copyId) {
   return checkIfContextAndCallbackLocked(st, copyId) || checkIfUserDataLocked(st, copyId);
}

//Mark the context of the quickfix list and the location lists (if present) as "in use". So that 
//garbage collection doesn't free the context.
pub Boole
llSetRef(int copyId) {
   if (!mainStackG)
      return true;
      
   Boole abort = false;
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      abort = abort || markReferencesInStack(locationStacksP + i, copyId);
      if (abort)
         return true;
   }
   return abort || memSetRefInCallback(&locationTextFnS, copyId);
}

//Return the autocmd name for the :lbook commands
private inline Arr(Byte)
getAutocmdNameForCbuffer(CommIndex id) {
   switch (id) {
   case C_lbook:   return S"lbook";
   case C_laddbook:   return S"laddbook";
   default: return NULL;
   }
}

//Process and validate the arguments passed to the :mbook, :maddbook,
//:lbook, :laddbook commands.
private int
processCbookArgs(Invocation* invo, OUT Book** outBook, LineNr* line1, LineNr* line2){
   Book* book = NULL;

   if (*invo->arg == ZERO)
      book = curBook;
   ei (*skipwhite(skipdigits(invo->arg)) == ZERO)
      book = bookFindFileByBookNr(atoi((char *)invo->arg));

   if (book == NULL) {
      emsg(_(e_invalid_argument));
      return FAIL;
   }

   if (bookNoMemfile(book)) {
      emsg(_(e_buffer_is_not_loaded));
      return FAIL;
   }

   if (invo->addr_count == 0) {
      invo->line1 = 1;
      invo->line2 = book->mem.lineCount;
   }

   if (invo->line1 < 1 || invo->line1 > book->mem.lineCount
       || invo->line2 < 1 || invo->line2 > book->mem.lineCount) {
      emsg(_(e_invalid_range));
      return FAIL;
   }

   *line1 = invo->line1;
   *line2 = invo->line2;
   *outBook = book;

   return OK;
}

//":[range]lbook [booknr]" command.
//":[range]laddbook [booknr]" command.
//":[range]lgetbook [booknr]" command.
pub void
c_lbook(Invocation* invo) {
   Book* book = NULL;
   LocationStack   *stack;
   int      res;
   Unt   idSave;
   LineNr   line1;
   LineNr   line2;

   CS auName = getAutocmdNameForCbuffer(invo->id);
   if (auName
         && applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()
   ) {
      return;
   }

   //Must come after autocommands.
   stack = locationStacksP + LOC_LIST_GREP;

   if (processCbookArgs(invo, OUT &book, &line1, &line2) == FAIL)
      return;

   CS title = copyCommandTitle(*invo->commline);

   if (book->shortFileName) {
      eeSnprintf(IObuff, IOSIZE, "%s (%s)", title, book->shortFileName);
      title = IObuff;
   }

   incrementLlBusyness();

   res = initAndUpdateTick(
      (Source){ .tag = SOURCE_BOOK, 
         .Book = (BookSource){.c = book, .start = line1, .end = line2 + 1}
      },
      OUT stack, book->o.errorFormat, (invo->id != C_laddbook), title
   );
   
   if (isStackEmpty(stack)) {
      decrementLlBusyness();
      return;
   }

   //Remember the current location list identifier, so that we can check for autocommands 
   //changing the current list.
   idSave = getCurrent(stack)->id;
   if (auName) {
      Book* curBookSaved = curBook;

      applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
      if (curBook != curBookSaved)
         //Autocommands changed book, don't jump now, "stack" may be invalid.
         res = 0;
   }
   //Jump to the first error for a new list and if autocmds didn't free the list.
   if (res > 0 && (invo->id == C_lbook) && isIdValid(stack, idSave)) {
      //display the first error
      jumpToFirstEntry(stack, idSave, invo->forceit);
   }

   decrementLlBusyness();
}

//Return the autocmd name for the :lexpr commands.
pub CS
cexpr_get_auname(CommIndex id) {
   switch (id) {
   case C_lexpr:     return S"lexpr";
   case C_lgetexpr:  return S"lgetexpr";
   case C_laddexpr:  return S"laddexpr";
   default:          return NULL;
   }
}

pub int
trigger_cexpr_autocmd(int id) {
   CS auName = cexpr_get_auname(id);

   if (auName
         && applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
   ) {
      if (aborting())
         return FAIL;
   }
   return OK;
}

pub int
cexpr_core(Invocation* invo, Var *tv) {
   LocationStack* stack = locationStacksP + LOC_LIST_GREP;

   if ((tv->tag == VAR_STRING && tv->string) || (tv->tag == VAR_LIST && tv->list)) {
      CS auName = cexpr_get_auname(invo->id);

      incrementLlBusyness();
      Source source = ((tv->tag == VAR_STRING && tv->string) 
         ? (Source){.tag = SOURCE_STRING, .String = (StringSource){.c = tv->string}}
         : (Source){.tag = SOURCE_LIST, .List = (ListSource){.c = tv->list->first}});
      
      int res = initAndUpdateTick(
         source, OUT stack, curBook->o.errorFormat, (invo->id != C_laddexpr), 
         copyCommandTitle(*invo->commline)
      );
      if (isStackEmpty(stack)) {
         decrementLlBusyness();
         return FAIL;
      }

      //Remember the current location list identifier, so that we can
      //check for autocommands changing the current list.
      Unt idSave = getCurrent(stack)->id;
      if (auName)
          applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);

      //Jump to the first error for a new list and if autocmds didn't free the list.
      if (res > 0 && (invo->id == C_lexpr) && isIdValid(stack, idSave))
         //display the first error
         jumpToFirstEntry(stack, idSave, invo->forceit);
      decrementLlBusyness();
      return OK;
   }

   emsg(_(e_string_or_list_expected));
   return FAIL;
}

//":mexpr {expr}", ":mgetexpr {expr}", ":maddexpr {expr}" command.
//":lexpr {expr}", ":lgetexpr {expr}", ":laddexpr {expr}" command.
//Also: ":maddexpr", ":mgetexpr", "laddexpr" and "laddexpr".
pub void
c_lExpr(Invocation* invo) {
   if (trigger_cexpr_autocmd(invo->id) == FAIL)
      return;

   //Evaluate the expression.  When the result is a string or a list we can
   //use it to fill the errorlist.
   Var* var = eval_expr(invo->arg, invo);
   if (!var)
      return;

   (void)cexpr_core(invo, var);
   freeVar(var);
}

//Search for a pattern in a help file.
private void
searchInFile(
   LocationList *ll,
   CS fname,
   OUT RegMatch *p_regmatch
){
   FILE* fd = fopen((char *)fname, "r");
   if (!fd)
      return;

   long lnum = 1;
   while (!eeFgets(IObuff, IOSIZE, fd) && !gotInterruptG) {
      CS line = IObuff;
      if (eeRegexec(OUT p_regmatch, line, (ColNr)0)) {
         int   l = (int)STRLEN(line);

         //remove trailing CR, LF, spaces, etc.
         while (l > 0 && line[l - 1] <= ' ')
            line[--l] = ZERO;

         if (addEntry(ll,
                  NULL,   //dir
                  fname,
                  NULL,
                  0,
                  line,
                  lnum,
                  0,
                  (int)(p_regmatch->startp[0] - line)
                  + 1,   //col
                  (int)(p_regmatch->endp[0] - line)
                  + 1,   //end_col
                  false,   //vis_col
                  NULL,   //search pattern
                  0,   //nr
                  1,   //type
                  NULL,   //user_data
                  true   //valid
            ) == QF_FAIL
         ) {
            gotInterruptG = true;
            if (line != IObuff)
               eeglFree(line);
            break;
         }
      }
      if (line != IObuff)
         eeglFree(line);
      ++lnum;
      line_breakcheck();
   }
   fclose(fd);
}

//Search for a pattern in all the help files in the doc directory under the given directory.
private void
searchFilesInDir(LocationList* ll, CS dirname, OUT RegMatch* p_regmatch, CS lang) {
   ExpandMatch files = {};
   files.a = createArena();

   //Find all "*.txt" and "*.??x" files in the "doc" directory.
   add_pathsep(dirname);
   STRCAT(dirname, "doc/*.\\(txt\\|??x\\)");
   if (gen_expand_wildcards(1, &dirname, EW_FILE|EW_SILENT, OUT &files) == OK 
         && files.len > 0
   ) {
      for (Unt fi = 0; fi < files.len && !gotInterruptG; ++fi) {
          //Skip files for a different language.
          if (lang != NULL
                && STRNICMP(lang, files.c[fi] + STRLEN(files.c[fi]) - 3, 2) != 0
                && !(STRNICMP(lang, "en", 2) == 0 
                   && STRNICMP("txt", files.c[fi] + STRLEN(files.c[fi]) - 3, 3) == 0)) {
             continue;
          } 

          searchInFile(ll, files.c[fi], OUT p_regmatch);
      }
   }
   deleteArena(files.a);
}

//":helpgrep {pattern}"
pub void
c_helpgrep(Invocation* invo) {
   int updated = false;

   CS auName = S"helpgrep";
   
   if (applyAutocomms(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()) {
      return;
   }

   LocationStack* stack = locationStacksP + LOC_LIST_HELP;

   incrementLlBusyness();

   //Check for a specified language
   CS lang = check_help_lang(invo->arg);
   RegMatch regmatch;
   regmatch.regprog = compileRegexp(invo->arg, RE_MAGIC + RE_STRING);
   regmatch.rm_ic = false;
   if (regmatch.regprog != NULL) {
      LocationList   *ll;

      newLocList(stack, copyCommandTitle(*invo->commline));
      ll = getCurrent(stack);

      searchFilesInDir(ll, PREFIX "/share/doc/eegl/", &regmatch, lang);

      eeRegFree(regmatch.regprog);

      ll->noValidEntries = false;
      ll->curr = ll->first;
      ll->currentIdx = 1;
      updateChangedTick(ll);
      updated = true;
   }

   if (updated) //This may open a portal and source scripts
      updateBook(stack, NULL);

   if (auName) {
      applyAutocomms(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
      //When adding a location list to an existing location list stack,
      //if the autocmd made the stack invalid, then just return.
      decrementLlBusyness();
      return;
   }

   //Jump to first match.
   if (!isEmpty(getCurrent(stack)))
      llJump(stack, 0, 0, false);
   else
      showErrFmtMsg(_(e_no_match_str_2), invo->arg);

   decrementLlBusyness();
}

# if defined(EXITFREE)
pub void
free_quickfix(void) {
   //Free all global location lists
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      freeAllLocLists(i);
   }
   ga_clear(&tempList);
}
# endif

//:getloclist m {specifics}
pub void
f_getloclist(Arr(Var) argvars, OUT Var* returnVar) {
   LocationStack* st = identifyStack(argvars);
   
   Var* specifics = argvars + 1;
   if (specifics == NULL) {
      allocReturnDict(returnVar);
      getProperties(st, NULL, returnVar->bag);
   } ei (specifics->tag == VAR_BAG) {
      Bag* specificss = specifics->bag;
      if (specificss) {
         allocReturnDict(returnVar);
         getProperties(st, specificss, returnVar->bag);
      } else {
         emsg(_(e_dictionary_required));
      }
   } else
      emsg(_(e_dictionary_required));
}

//Set the location stack's current list's contents. Used by "setloclist()" script fn
private void
setLocationListInternal(
   LocationStack* stack,
   Var* listArg,
   Var* actionArg,
   Var* specificArg,
   Var* returnVar
){
   static int   recursive = 0;

   returnVar->number = -1;

   if (listArg->tag != VAR_LIST)
      emsg(_(e_list_required));
   ei (recursive != 0)
      emsg(_(e_autocommand_caused_recursive_behavior));
   else {
      List* newContent = listArg->list;
      Bag* specific = NULL;
      Boole isDictValid = true;

      LocListAction action = LL_ACTION_INVALID;
      
      if (actionArg->tag == VAR_STRING) {
         CS act = convertVarToStringSingleUse(actionArg);
         if (act == NULL)
            return;      //type error; errmsg already given
            
         if (act[0] != ZERO && act[1] == ZERO) {
            switch(act[0]){
            case 'a': action = LL_ACTION_ADD; break;
            case 'r': action = LL_ACTION_REPLACE; break;
            case 'u': action = LL_ACTION_UPDATE; break;
            case ' ': action = LL_ACTION_NEW; break;
            case 'f': action = LL_ACTION_FREE; break;
            }
         } 
         if (action == LL_ACTION_INVALID)   
            showErrFmtMsg(_(e_invalid_action_str_1), act);
      } ei (actionArg->tag == VAR_UNKNOWN)
         action = LL_ACTION_NEW;
      else
         emsg(_(e_string_required));

      if (actionArg->tag != VAR_UNKNOWN && specificArg->tag != VAR_UNKNOWN) {
         if (specificArg->tag == VAR_BAG && specificArg->bag)
            specific = specificArg->bag;
         else {
            emsg(_(e_dictionary_required));
            isDictValid = false;
         }
      }

      ++recursive;
      if (newContent 
            && action != LL_ACTION_INVALID && isDictValid
            && setLocationList(stack, newContent, action, (CS)":setloclist()", specific) == OK
      )
         returnVar->number = 0;
      --recursive;
   }
}

pub void
f_setloclist(Var* argvars, Var* returnVar){
   returnVar->number = -1;
   LocationStack* st= identifyStack(argvars);
   if (st)
      setLocationListInternal(st, &argvars[1], &argvars[2], &argvars[3], returnVar);
}

//}}}
//{{{marks

//This file contains routines to maintain and manipulate marks.

//If a named file mark's lnum is non-zero, it is valid.
//If a named file mark's fnum is non-zero, it is for an existing book,
//otherwise it is from .eeglinfo and namedfm[n].fname is the file name.
//There are marks 'A - 'Z (set by user) and '0 to '9 (set when writing eeglinfo).
private FileMarkExt namedfm[NMARKS + EXTRA_MARKS];      //marks with file nr

private void fname2fnum(FileMarkExt *fm);
private void fmarks_check_one(FileMarkExt *fm, Byte *name, Book *book);
private CS mark_line(Pos* mp, int lead_len);
private void show_one_mark(int, Byte *, Pos *, Byte *, int current);

//Set named mark "c" at current cursor position. Return OK on success, FAIL if bad name given.
pub int
setmark(int c) {
   return setmark_pos(c, &curPor->cursor, curBook->fiNum);
}

//Set named mark "c" to position "pos". When "c" is upper case use file "fnum".
//Return OK on success, FAIL if bad name given.
pub int
setmark_pos(int c, Pos *pos, int fnum) {
   int      i;

   //Check for a special key (may cause islower() to crash).
   if (c < 0)
      return FAIL;

   if (c == '\'' || c == '`') {
      if (pos == &curPor->cursor) {
         setpcmark();
         //keep it even when the cursor doesn't move
         curPor->prevPrevContextMark = curPor->prevContextMark;
      } else
          curPor->prevContextMark = *pos;
      return OK;
   }

   Book* book = bookFindFileByBookNr(fnum);
   if (!book)
      return FAIL;

   if (c == '"') {
      book->lastCursor = *pos;
      return OK;
   }

   //Allow setting '[ and '] for an autocommand that simulates reading a file.
   if (c == '[') {
      book->opStart = *pos;
      return OK;
   }
   if (c == ']') {
      book->opEnd = *pos;
      return OK;
   }

   if (c == '<' || c == '>') {
      if (c == '<')
         book->visual.vi_start = *pos;
      else
         book->visual.vi_end = *pos;
      if (book->visual.vi_mode == ZERO)
         //Visual_mode has not yet been set, use a sane default.
         book->visual.vi_mode = 'v';
      return OK;
   }

   if (ASCII_ISLOWER(c)) {
      i = c - 'a';
      book->namedMarks[i] = *pos;
      return OK;
   }
   if (ASCII_ISUPPER(c) || EE_ISDIGIT(c)) {
      if (EE_ISDIGIT(c))
         i = c - '0' + NMARKS;
      else
         i = c - 'A';
      namedfm[i].fmark.mark = *pos;
      namedfm[i].fmark.fnum = fnum;
      EE_CLEAR(namedfm[i].fname);
      namedfm[i].time_set = eeTime();
      return OK;
   }
   return FAIL;
}

//Delete every entry referring to file 'fnum' from both the jumplist and the tag stack.
pub void
mark_forget_file(Portal *wp, int fnum) {

   for (int i = wp->jumpListLen - 1; i >= 0; --i) {
      if (wp->jumpList[i].fmark.fnum == fnum) {
          eeglFree(wp->jumpList[i].fname);
          if (wp->jumpListInd > i)
         --wp->jumpListInd;
          --wp->jumpListLen;
          MEMMOVE(&wp->jumpList[i], &wp->jumpList[i + 1],
            (wp->jumpListLen - i) * sizeof(wp->jumpList[i]));
      }
   } 

   for (Unt i = wp->tagStackLen - 1; i < wp->tagStackLen; --i) {
      if (wp->tagStack[i].fmark.fnum == fnum) {
         tagstack_clear_entry(&wp->tagStack[i]);
         if (wp->tagStackInd > i)
            --wp->tagStackInd;
         --wp->tagStackLen;
         MEMMOVE(&wp->tagStack[i], &wp->tagStack[i + 1],
            (wp->tagStackLen - i) * sizeof(wp->tagStack[i]));
      }
   } 
}

//Set the previous context mark to the current position and add it to the jump list.
pub void
setpcmark(void) {
   //for :global the mark is set only once
   if (global_busy || listcmd_busy || (commModifierG.cmod_flags & CMOD_KEEPJUMPS))
      return;

   curPor->prevPrevContextMark = curPor->prevContextMark;
   curPor->prevContextMark = curPor->cursor;

   //If we're somewhere in the middle of the jumplist, discard everything after the current index.
   if (curPor->jumpListInd < curPor->jumpListLen - 1)
       //Discard the rest of the jumplist by cutting the length down to
       //contain nothing beyond the current index.
       curPor->jumpListLen = curPor->jumpListInd + 1;

   //If jumplist is full: remove oldest entry
   if (++curPor->jumpListLen > JUMPLISTSIZE) {
      curPor->jumpListLen = JUMPLISTSIZE;
      eeglFree(curPor->jumpList[0].fname);
      for (int i = 1; i < JUMPLISTSIZE; ++i)
         curPor->jumpList[i - 1] = curPor->jumpList[i];
   }
   curPor->jumpListInd = curPor->jumpListLen;
   FileMarkExt* fm = &curPor->jumpList[curPor->jumpListLen - 1];

   fm->fmark.mark = curPor->prevContextMark;
   fm->fmark.fnum = curBook->fiNum;
   fm->fname = NULL;
   fm->time_set = eeTime();
}

//To change context, call setpcmark(), then move the current position to
//where ever, then call checkpcmark().  This ensures that the previous
//context will only be changed if the cursor moved to a different line.
//If pcmark was deleted (with "dG") the previous mark is restored.
pub void
checkpcmark(void) {
   if (curPor->prevPrevContextMark.lnum != 0
          && (EQUAL_POS(curPor->prevContextMark, curPor->cursor)
         || curPor->prevContextMark.lnum == 0))
      curPor->prevContextMark = curPor->prevPrevContextMark;
   curPor->prevPrevContextMark.lnum = 0;      //it has been checked
}

//move "count" positions in the jump list (count may be negative)
pub Pos *
movemark(int count) {
   Pos   *pos;
   FileMarkExt   *jmp;

   cleanup_jumplist(curPor, true);

   if (curPor->jumpListLen == 0)       //nothing to jump to
      return (Pos *)NULL;

   for (;;) {
      if (curPor->jumpListInd + count < 0
            || curPor->jumpListInd + count >= curPor->jumpListLen)
         return (Pos *)NULL;

      //if first CTRL-O or CTRL-I command after a jump, add cursor position
      //to list.  Careful: If there are duplicates (CTRL-O immediately after
      //starting Eegl on a file), another entry may have been removed.
      if (curPor->jumpListInd == curPor->jumpListLen) {
         setpcmark();
         --curPor->jumpListInd;   //skip the new entry
         if (curPor->jumpListInd + count < 0)
            return (Pos *)NULL;
      }

      curPor->jumpListInd += count;

      jmp = curPor->jumpList + curPor->jumpListInd;
      if (jmp->fmark.fnum == 0)
         fname2fnum(jmp);
      if (jmp->fmark.fnum != curBook->fiNum) {
         //Make a copy, an autocommand may make "jmp" invalid.
         FileMark fmark = jmp->fmark;

         //jump to the file with the mark
         if (bookFindFileByBookNr(fmark.fnum) == NULL) {                    //Skip this one ..
            count += count < 0 ? -1 : 1;
            continue;
         }
         if (booklistGetFile(fmark.fnum, fmark.mark.lnum, 0, false) == FAIL)
            return (Pos *)NULL;
         //Set lnum again, autocommands my have changed it
         curPor->cursor = fmark.mark;
         pos = (Pos *)-1;
      } else
         pos = &(jmp->fmark.mark);
      return pos;
   }
}

//Move "count" positions in the changelist (count may be negative).
pub Pos *
movechangelist(int count) {
   if (curBook->changeListLen == 0)       //nothing to jump to
      return (Pos *)NULL;

   int n = curPor->changeListInd;
   if (n + count < 0) {
      if (n == 0)
          return (Pos *)NULL;
      n = 0;
   } ei (n + count >= (int)curBook->changeListLen) {
      if (n == (int)curBook->changeListLen - 1)
          return (Pos *)NULL;
      n = curBook->changeListLen - 1;
   } else
      n += count;
   curPor->changeListInd = n;
   return curBook->changeList + n;
}

//Find mark "c" in book pointed to by "book".
//If "changefile" is true it's allowed to edit another file for '0, 'A, etc.
//If "fnum" is not NULL store the fnum there for '0, 'A etc., don't edit another file.
//Return:
//- pointer to Pos if found.  lnum is 0 when mark not set, -1 when mark is
// in another file which can't be gotten. (caller needs to check lnum!)
//- NULL if there is no mark called 'c'.
//- -1 if mark is in other file and jumped there (only if changefile is true)
pub Pos *
markGetBook(Book* book, int c, int changefile) {
   return markGetBookFnum(book, c, changefile, NULL);
}

pub Pos *
getmark(int c, int changefile) {
   return markGetBookFnum(curBook, c, changefile, NULL);
}

pub Pos *
markGetBookFnum(Book* book, int c, int changefile, int* fnum) {
   Pos *startp, *endp;
   static Pos pos_copy;

   Pos* posp = NULL;

   //Check for special key, can't be a mark name and might cause islower() to crash.
   if (c < 0)
      return posp;
   if (c > '~')         //check for islower()/isupper()
      ;
   ei (c == '\'' || c == '`') {  //previous context mark
      pos_copy = curPor->prevContextMark;   //need to make a copy because
      posp = &pos_copy;      //  prevContextMark may be changed soon
   } ei (c == '"')         //to pos when leaving buffer
      posp = &(book->lastCursor);
   ei (c == '^')         //to where Insert mode stopped
      posp = &(book->lastInsert);
   ei (c == '.')         //to where last change was made
      posp = &(book->lastChange);
   ei (c == '[')         //to start of previous operator
      posp = &(book->opStart);
   ei (c == ']')         //to end of previous operator
      posp = &(book->opEnd);
   ei (c == '{' || c == '}') {  //to previous/next paragraph
      Operator   oa;
      int   slcb = listcmd_busy;

      Pos pos = curPor->cursor;
      listcmd_busy = true;       //avoid that '' is changed
      if (normFindNextParagraf(&oa.inclusive, c == '}' ? FORWARD : BACKWARD, 1L, ZERO, false)) {
         pos_copy = curPor->cursor;
         posp = &pos_copy;
      }
      curPor->cursor = pos;
      listcmd_busy = slcb;
   } ei (c == '(' || c == ')') {  //to previous/next sentence
      int   slcb = listcmd_busy;

      Pos pos = curPor->cursor;
      listcmd_busy = true;       //avoid that '' is changed
      if (findsent(c == ')' ? FORWARD : BACKWARD, 1L)) {
         pos_copy = curPor->cursor;
         posp = &pos_copy;
      }
      curPor->cursor = pos;
      listcmd_busy = slcb;
   } ei (c == '<' || c == '>') {  //start/end of visual area
      startp = &book->visual.vi_start;
      endp = &book->visual.vi_end;
      if (((c == '<') == LT_POS(*startp, *endp) || endp->lnum == 0) && startp->lnum != 0)
         posp = startp;
      else
         posp = endp;
      //For Visual line mode, set mark at begin or end of line
      if (book->visual.vi_mode == 'V') {
         pos_copy = *posp;
         posp = &pos_copy;
         if (c == '<')
            pos_copy.col = 0;
         else
            pos_copy.col = MAXCOL;
         pos_copy.coladd = 0;
      }
   } ei (ASCII_ISLOWER(c))  {    //normal named mark
      posp = &(book->namedMarks[c - 'a']);
   } ei (ASCII_ISUPPER(c) || EE_ISDIGIT(c)) {  //named file mark
      if (EE_ISDIGIT(c))
         c = c - '0' + NMARKS;
      else
         c -= 'A';
      posp = &(namedfm[c].fmark.mark);

      if (namedfm[c].fmark.fnum == 0)
         fname2fnum(&namedfm[c]);

      if (fnum)
         *fnum = namedfm[c].fmark.fnum;
      ei (namedfm[c].fmark.fnum != book->fiNum) {
         //mark is in another file
         posp = &pos_copy;

         if (namedfm[c].fmark.mark.lnum != 0 && changefile && namedfm[c].fmark.fnum) {
            if (booklistGetFile(namedfm[c].fmark.fnum, (LineNr)1, GETF_SETMARK, false) == OK) {
               //Set the lnum now, autocommands could have changed it
               curPor->cursor = namedfm[c].fmark.mark;
               return (Pos *)-1;
            }
            pos_copy.lnum = -1;   //can't get file
         } else
            pos_copy.lnum = 0;   //mark exists, but is not valid in current buffer
      }
   }

   return posp;
}

//Search for the next named mark in the current file.
//
//Return pointer to Pos of the next mark or NULL if no mark is found.
pub Pos *
getnextmark(Pos* startpos, Unt dir, int begin_line) {
   int      i;
   Pos   *result = NULL;

   Pos pos = *startpos;

   //When searching backward and leaving the cursor on the first non-blank,
   //position must be in a previous line.
   //When searching forward and leaving the cursor on the first non-blank,
   //position must be in a next line.
   if (dir == BACKWARD && begin_line)
      pos.col = 0;
   ei (dir == FORWARD && begin_line)
      pos.col = MAXCOL;

   for (i = 0; i < NMARKS; i++) {
      if (curBook->namedMarks[i].lnum > 0) {
         if (dir == FORWARD) {
         if ((result == NULL || LT_POS(curBook->namedMarks[i], *result))
               && LT_POS(pos, curBook->namedMarks[i]))
            result = &curBook->namedMarks[i];
         } else {
            if ((result == NULL || LT_POS(*result, curBook->namedMarks[i]))
                  && LT_POS(curBook->namedMarks[i], pos))
               result = &curBook->namedMarks[i];
         }
      }
   }

   return result;
}

//For an xtended filemark: set the fnum from the fname.
//This is used for marks obtained from the .eeglinfo file.  It's postponed
//until the mark is used to avoid a long startup delay.
private void
fname2fnum(FileMarkExt* fm) {
   if (!fm->fname)
      return;

   //First expand "~/" in the file name to the home directory.
   //Don't expand the whole name since it may contain other '~' chars.
   if (fm->fname[0] == '~' && (fm->fname[1] == '/')) {
      Unt len = doExpandEnv(OUT nameBuffTextG, S"~/");
      copySubstrToAllocation(nameBuffG + len, (Text){fm->fname + 2, MAXPATHL - len - 1});
   } else
      copySubstrToAllocation(nameBuffG, (Text){fm->fname, MAXPATHL - 1});

   //Try to shorten the file name.
   mch_dirname(IObuff, IOSIZE);
   CS p = shorten_fname(nameBuffG, IObuff);

   //bookNew() will call fmarks_check_names()
   (void)bookNew(nameBuffG, p, (LineNr)1, 0);
}

//Check all file marks for a name that matches the file name in book. May replace the name with an 
//fnum. Used for marks that come from the .eeglinfo file.
pub void
fmarks_check_names(Book* book) {
   if (book->fullFileName == NULL)
      return;

   CS name = home_replace_save(book, book->fullFileName);
   if (!name)
      return;

   for (int i = 0; i < NMARKS + EXTRA_MARKS; ++i)
      fmarks_check_one(&namedfm[i], name, book);

   Portal   *wp;
   FOR_ALL_PORTALS(wp) {
      for (int i = 0; i < wp->jumpListLen; ++i)
         fmarks_check_one(&wp->jumpList[i], name, book);
   }

   eeglFree(name);
}

private void
fmarks_check_one(FileMarkExt* fm, CS name, Book* book) {
   if (fm->fmark.fnum == 0 && fm->fname != NULL && fnamecmp(name, fm->fname) == 0) {
      fm->fmark.fnum = book->fiNum;
      EE_CLEAR(fm->fname);
   }
}

//Check a if a position from a mark is valid.
//Give and error message and return FAIL if not.
pub int
check_mark(Pos* pos) {
   if (!pos) {
      emsg(_(e_unknown_mark));
      return FAIL;
   }
   if (pos->lnum <= 0) {
      //lnum is negative if mark is in another file can can't get that
      //file, error message already give then.
      if (pos->lnum == 0)
         emsg(_(e_mark_not_set));
      return FAIL;
   }
   if (pos->lnum > curBook->mem.lineCount) {
      emsg(_(e_mark_has_invalid_line_number));
      return FAIL;
   }
   return OK;
}

//clrallmarks() - clear all marks in the book 'book'
//
//Used mainly when trashing the entire book during ":e" type commands
pub void
clrallmarks(Book* book) {
   static int i = -1;

   if (i == -1) {   //first call ever: initialize
      for (i = 0; i < NMARKS + 1; i++) {
         namedfm[i].fmark.mark.lnum = 0;
         namedfm[i].fname = NULL;
         namedfm[i].time_set = 0;
      }
   } 

   for (i = 0; i < NMARKS; i++)
      book->namedMarks[i].lnum = 0;
   book->opStart.lnum = 0;      //start/end op mark cleared
   book->opEnd.lnum = 0;
   book->lastCursor.lnum = 1;   //'" mark cleared
   book->lastCursor.col = 0;
   book->lastCursor.coladd = 0;
   book->lastInsert.lnum = 0;   //'^ mark cleared
   book->lastChange.lnum = 0;   //'. mark cleared
   book->changeListLen = 0;
}

//Get name of file from a filemark.
//When it's in the current buffer, return the text at the mark. Returns an allocated string.
pub CS
fm_getname(FileMark* fmark, int lead_len) {
   if (fmark->fnum == curBook->fiNum)          //current buffer
      return mark_line(&(fmark->mark), lead_len);
   return bookGetNameByBookNr(fmark->fnum, false, true);
}

//Return the line at mark "mp". Truncate to fit in portal. The returned string has been allocated
private CS
mark_line(Pos* mp, int lead_len) {
   if (mp->lnum == 0 || mp->lnum > curBook->mem.lineCount)
      return copyStr(S"-invalid-");
   //Allow for up to 5 bytes per character.
   CS s = copySubstr(skipwhite(ml_get(mp->lnum)), visibleColsG * 5);
   
   //Truncate the line to fit it in the portal.
   int len = 0;
   CS p;
   for (p = s; *p != ZERO; MB_PTR_ADV(p)) {
      len += bookPtr2Cells(p);
      if (len >= visibleColsG - lead_len)
         break;
   }
   *p = ZERO;
   return s;
}

//print the marks
pub void
c_marks(Invocation *invo) {
   CS arg = invo->arg;
   int i;
   CS name;
   Pos   *posp, *startp, *endp;

   if (arg && *arg == ZERO)
      arg = NULL;

   show_one_mark('\'', arg, &curPor->prevContextMark, NULL, true);
   for (i = 0; i < NMARKS; ++i)
      show_one_mark(i + 'a', arg, &curBook->namedMarks[i], NULL, true);
   for (i = 0; i < NMARKS + EXTRA_MARKS; ++i) {
      if (namedfm[i].fmark.fnum != 0)
         name = fm_getname(&namedfm[i].fmark, 15);
      else
         name = namedfm[i].fname;
      if (name != NULL) {
         show_one_mark(i >= NMARKS ? i - NMARKS + '0' : i + 'A',
             arg, &namedfm[i].fmark.mark, name,
             namedfm[i].fmark.fnum == curBook->fiNum
         );
         if (namedfm[i].fmark.fnum != 0)
            eeglFree(name);
      }
   }
   show_one_mark('"', arg, &curBook->lastCursor, NULL, true);
   show_one_mark('[', arg, &curBook->opStart, NULL, true);
   show_one_mark(']', arg, &curBook->opEnd, NULL, true);
   show_one_mark('^', arg, &curBook->lastInsert, NULL, true);
   show_one_mark('.', arg, &curBook->lastChange, NULL, true);

   //Show the marks as where they will jump to.
   startp = &curBook->visual.vi_start;
   endp = &curBook->visual.vi_end;
   if ((LT_POS(*startp, *endp) || endp->lnum == 0) && startp->lnum != 0)
      posp = startp;
   else
      posp = endp;
   show_one_mark('<', arg, posp, NULL, true);
   show_one_mark('>', arg, posp == startp ? endp : startp, NULL, true);

   show_one_mark(-1, arg, NULL, NULL, false);
}

private void
show_one_mark(
   int c,
   CS arg,
   Pos* p,
   CS name_arg,
   int current   //in current file
){
   static int   did_title = false;
   int      mustfree = false;
   CS name = name_arg;

   if (c == -1) {            //finish up
      if (did_title)
         did_title = false;
      else {
         if (arg == NULL)
            msg(_("No marks set"));
         else
            showErrFmtMsg(_(e_no_marks_matching_str), arg);
      }
   }
   //don't output anything if 'q' typed at --more-- prompt
   ei (!gotInterruptG && (!arg || firstOccurrence(arg, c) != NULL) && p->lnum != 0) {
      if (!name && current) {
         name = mark_line(p, 15);
         if (!name) {
            emsg(_(e_out_of_memory));
            return;
         }
         mustfree = true;
      }
      if (!message_filtered(name)) {
         if (!did_title) {
            //Highlight title
            msg_puts_title(_("\nmark line  col file/text"));
            did_title = true;
         }
         msg_putchar('\n');
         if (!gotInterruptG) {
            sprintf((char *)IObuff, " %c " FMT_UNT " %4d ", c, p->lnum, p->col);
            msg_outtrans(IObuff);
            if (name) {
               msgOuttransDeco(name, current ? getDecoFlags(HLF_D) : 0);
            }
         }
         out_flush();          //show one line at a time
      }
      if (mustfree)
         eeglFree(name);
   }
}

//":delmarks[!] [marks]"
pub void
c_delmarks(Invocation* invo) {
   int from, to;
   int i;
   int lower;
   int digit;
   int n;

   if (*invo->arg == ZERO && invo->forceit)
      //clear all marks
      clrallmarks(curBook);
   ei (invo->forceit)
      emsg(_(e_invalid_argument));
   ei (*invo->arg == ZERO)
      emsg(_(e_argument_required));
   else {
      //clear specified marks only
      for (CS p = invo->arg; *p != ZERO; ++p) {
         lower = ASCII_ISLOWER(*p);
         digit = EE_ISDIGIT(*p);
         if (lower || digit || ASCII_ISUPPER(*p)) {
            if (p[1] == '-') {
               //clear range of marks
               from = *p;
               to = p[2];
               if (!(lower ? ASCII_ISLOWER(p[2])
                  : (digit ? EE_ISDIGIT(p[2]) : ASCII_ISUPPER(p[2])))
                   || to < from
               ){
                  showErrFmtMsg(_(e_invalid_argument_str), p);
                  return;
               }
               p += 2;
            } else
               //clear one lower case mark
               from = to = *p;

            for (i = from; i <= to; ++i) {
               if (lower)
                  curBook->namedMarks[i - 'a'].lnum = 0;
               else {
                  if (digit)
                     n = i - '0' + NMARKS;
                  else
                     n = i - 'A';
                  namedfm[n].fmark.mark.lnum = 0;
                  namedfm[n].fmark.fnum = 0;
                  EE_CLEAR(namedfm[n].fname);
                  namedfm[n].time_set = digit ? 0 : eeTime();
               }
            }
         } else {
            switch (*p) {
            case '"': curBook->lastCursor.lnum = 0; break;
            case '^': curBook->lastInsert.lnum = 0; break;
            case '.': curBook->lastChange.lnum = 0; break;
            case '[': curBook->opStart.lnum    = 0; break;
            case ']': curBook->opEnd.lnum      = 0; break;
            case '<': curBook->visual.vi_start.lnum = 0; break;
            case '>': curBook->visual.vi_end.lnum   = 0; break;
            case ' ': break;
            default:  showErrFmtMsg(_(e_invalid_argument_str), p);
                 return;
            }
         } 
      }
   }
}

//print the jumplist
pub void
c_jumps(Invocation*) {
   CS name;

   cleanup_jumplist(curPor, true);

   //Highlight title
   msg_puts_title(_("\n jump line  col file/text"));
   for (int i = 0; i < curPor->jumpListLen && !gotInterruptG; ++i) {
      if (curPor->jumpList[i].fmark.mark.lnum != 0) {
         name = fm_getname(&curPor->jumpList[i].fmark, 16);

         //Make sure to output the current indicator, even when on a wiped
         //out book.  ":filter" may still skip it.
         if (name == NULL && i == curPor->jumpListInd)
            name = copyStr((CS)"-invalid-");
         //apply :filter /pat/ or file name not available
         if (name == NULL || message_filtered(name)) {
            eeglFree(name);
            continue;
         }

         msg_putchar('\n');
         if (gotInterruptG) {
            eeglFree(name);
            break;
         }
         sprintf(
            (char *)IObuff, "%c %2d " FMT_UNT " %4d ",
            i == curPor->jumpListInd ? '>' : ' ',
            i > curPor->jumpListInd ? i - curPor->jumpListInd
                       : curPor->jumpListInd - i,
            curPor->jumpList[i].fmark.mark.lnum,
            curPor->jumpList[i].fmark.mark.col
         );
         msg_outtrans(IObuff);
         msgOuttransDeco(
            name, curPor->jumpList[i].fmark.fnum == curBook->fiNum ? getDecoFlags(HLF_D) : 0
         );
         eeglFree(name);
         ui_breakcheck();
      }
      out_flush();
   }
   if (curPor->jumpListInd == curPor->jumpListLen)
      msg_puts(S"\n>");
}

pub void
c_clearjumps(Invocation*) {
   free_jumplist(curPor);
   curPor->jumpListLen = 0;
   curPor->jumpListInd = 0;
}

//print the changelist
pub void
c_changes(Invocation*) {
   CS name;

   //Highlight title
   msg_puts_title(_("\nchange line  col text"));

   for (Unt i = 0; i < curBook->changeListLen && !gotInterruptG; ++i) {
      if (curBook->changeList[i].lnum != 0) {
         msg_putchar('\n');
         if (gotInterruptG)
            break;
         sprintf((char *)IObuff, "%c %3d %5ld %4d ",
             i == (Unt)curPor->changeListInd ? '>' : ' ',
             i > (Unt)curPor->changeListInd ? i - curPor->changeListInd
                     : curPor->changeListInd - i,
             (long)curBook->changeList[i].lnum,
             curBook->changeList[i].col);
         msg_outtrans(IObuff);
         name = mark_line(&curBook->changeList[i], 17);
         if (!name)
            break;
         msgOuttransDeco(name, getDecoFlags(HLF_D));
         eeglFree(name);
         ui_breakcheck();
      }
      out_flush();
   }
   if (curPor->changeListInd == (int)curBook->changeListLen)
      msg_puts(S"\n>");
}

#define one_adjust(add) \
    { \
   lp = add; \
   if (*lp >= line1 && *lp <= line2) \
   { \
       if (amount == MAXLNUM) \
      *lp = 0; \
       else \
      *lp += amount; \
   } \
   ei (amount_after && *lp > line2) \
       *lp += amount_after; \
    }

//don't delete the line, just put at first deleted line
#define one_adjust_nodel(add) \
    { \
   lp = add; \
   if (*lp >= line1 && *lp <= line2) \
   { \
       if (amount == MAXLNUM) \
      *lp = line1; \
       else \
      *lp += amount; \
   } \
   ei (amount_after && *lp > line2) \
       *lp += amount_after; \
    }

//Adjust marks between "line1" and "line2" (inclusive) to move "amount" lines. Must be called before
//changed_*(), appended_lines() or deleted_lines(). May be called before or after changing the text.
//When deleting lines "line1" to "line2", use an "amount" of MAXLNUM: then the marks within this 
//range are made invalid.
//If "amount_after" is non-zero adjust, marks after "line2".
//Example: Delete lines 34 and 35: markAdjust(34, 35, MAXLNUM, -2, true);
//Example: Insert two lines below 55: markAdjust(56, MAXLNUM, 2, 0, true);
//             or: markAdjust(56, 55, MAXLNUM, 2, true);
pub void
markAdjust(
   LineNr line1,
   LineNr line2,
   long amount,
   long amount_after,
   Boole adjust_folds
) {
   int i;
   int fnum = curBook->fiNum;
   LineNr* lp;
   static Pos initpos = {1, 0, 0};

   if (line2 < line1 && amount_after == 0L)       //nothing to do
      return;

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      //named marks, lower case and upper case
      for (i = 0; i < NMARKS; i++) {
         one_adjust(&(curBook->namedMarks[i].lnum));
         if (namedfm[i].fmark.fnum == fnum)
            one_adjust_nodel(&(namedfm[i].fmark.mark.lnum));
      }
      for (i = NMARKS; i < NMARKS + EXTRA_MARKS; i++) {
         if (namedfm[i].fmark.fnum == fnum)
            one_adjust_nodel(&(namedfm[i].fmark.mark.lnum));
      }

      //last Insert position
      one_adjust(&(curBook->lastInsert.lnum));

      //last change position
      one_adjust(&(curBook->lastChange.lnum));

      //last cursor position, if it was set
      if (!EQUAL_POS(curBook->lastCursor, initpos))
         one_adjust(&(curBook->lastCursor.lnum));

      //list of change positions
      for (Unt i = 0; i < curBook->changeListLen; ++i)
          one_adjust_nodel(&(curBook->changeList[i].lnum));

      //Visual area
      one_adjust_nodel(&(curBook->visual.vi_start.lnum));
      one_adjust_nodel(&(curBook->visual.vi_end.lnum));

      //location list marks
      llAdjustEntries(line1, line2, amount, amount_after);
      sign_mark_adjust(line1, line2, amount, amount_after);
   }

   //previous context mark
   one_adjust(&(curPor->prevContextMark.lnum));

   //previous pcmark
   one_adjust(&(curPor->prevPrevContextMark.lnum));

   //saved cursor for formatting
   if (saved_cursor.lnum != 0)
      one_adjust_nodel(&(saved_cursor.lnum));

   //Adjust items in all portals into the current buffer
   
   Portal* port;
   Tab* tab;
   FOR_ALL_TAB_PORTALS(tab, port) {
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
         //Marks in the jumplist.  When deleting lines, this may create
         //duplicate marks in the jumplist, they will be removed later.
         for (i = 0; i < port->jumpListLen; ++i) {
            if (port->jumpList[i].fmark.fnum == fnum)
                one_adjust_nodel(&(port->jumpList[i].fmark.mark.lnum));
         } 

      if (port->book == curBook) {
         if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
            //marks in the tag stack
            for (Unt i = 0; i < port->tagStackLen; i++) {
               if (port->tagStack[i].fmark.fnum == fnum)
                  one_adjust_nodel(&(port->tagStack[i].fmark.mark.lnum));
            } 
         } 

         //the displayed Visual area
         if (port->prevVisualEnd != 0) {
            one_adjust_nodel(&(port->prevVisualEnd));
            one_adjust_nodel(&(port->oldVisualLnum));
         }

         //topline and cursor position for portals into the same buffer other than curPor
         if (port != curPor) {
            if (port->topLine >= line1 && port->topLine <= line2) {
               if (amount == MAXLNUM) {    //topline is deleted
                  if (line1 <= 1)
                      port->topLine = 1;
                  else
                      port->topLine = line1 - 1;
               } ei (port->topLine > line1)
                  //Keep topline on the same line, unless inserting just
                  //above it (we probably want to see that line then)
                  port->topLine += amount;
               port->topFill = 0;
            } ei (amount_after && port->topLine > line2) {
               port->topLine += amount_after;
               port->topFill = 0;
            }
            if (port->cursor.lnum >= line1 && port->cursor.lnum <= line2) {
               if (amount == MAXLNUM) { //line with cursor is deleted
                  if (line1 <= 1)
                     port->cursor.lnum = 1;
                  else
                     port->cursor.lnum = line1 - 1;
                  port->cursor.col = 0;
               } else      //keep cursor on the same line
                  port->cursor.lnum += amount;
            } ei (amount_after && port->cursor.lnum > line2)
                port->cursor.lnum += amount_after;
         }

         //adjust folds
         if (adjust_folds)
            foldMarkAdjust(port, line1, line2, amount, amount_after);
      }
   }

   //adjust diffs
   diff_mark_adjust(line1, line2, amount, amount_after);
}

//This code is used often, needs to be fast.
#define col_adjust(pp) \
    { \
   posp = pp; \
   if (posp->lnum == lnum && posp->col >= mincol) \
   { \
       posp->lnum += lnum_amount; \
       if (col_amount < 0 && posp->col <= (ColNr)-col_amount) \
      posp->col = 0; \
       ei (posp->col < spaces_removed) \
      posp->col = col_amount + spaces_removed; \
       else \
      posp->col += col_amount; \
   } \
    }

//Adjust marks in line "lnum" at column "mincol" and further: add
//"lnum_amount" to the line number and add "col_amount" to the column position.
//"spaces_removed" is the number of spaces that were removed, matters when the cursor is inside 
//them
pub void
mark_col_adjust(
   LineNr lnum,
   ColNr mincol,
   long lnum_amount,
   long col_amount,
   int spaces_removed
) {
   int i;
   int fnum = curBook->fiNum;

   if ((col_amount == 0L && lnum_amount == 0L) || (commModifierG.cmod_flags & CMOD_LOCKMARKS))
      return; //nothing to do

   //named marks, lower case and upper case
   Pos* posp;
   for (i = 0; i < NMARKS; i++) {
      col_adjust(&(curBook->namedMarks[i]));
      if (namedfm[i].fmark.fnum == fnum)
         col_adjust(&(namedfm[i].fmark.mark));
   }
   for (i = NMARKS; i < NMARKS + EXTRA_MARKS; i++) {
      if (namedfm[i].fmark.fnum == fnum)
         col_adjust(&(namedfm[i].fmark.mark));
   }

   //last Insert position
   col_adjust(&(curBook->lastInsert));

   //last change position
   col_adjust(&(curBook->lastChange));

   //list of change positions
   for (Unt i = 0; i < curBook->changeListLen; ++i)
     col_adjust(&(curBook->changeList[i]));

   //Visual area
   col_adjust(&(curBook->visual.vi_start));
   col_adjust(&(curBook->visual.vi_end));

   //previous context mark
   col_adjust(&(curPor->prevContextMark));

   //previous pcmark
   col_adjust(&(curPor->prevPrevContextMark));

   //saved cursor for formatting
   col_adjust(&saved_cursor);

   //Adjust items in all portals into the current buffer.
   Portal* port;
   FOR_ALL_PORTALS(port) {
      //marks in the jumplist
      for (i = 0; i < port->jumpListLen; ++i)
         if (port->jumpList[i].fmark.fnum == fnum)
            col_adjust(&(port->jumpList[i].fmark.mark));

      if (port->book == curBook) {
         //marks in the tag stack
         for (Unt i = 0; i < port->tagStackLen; i++) {
            if (port->tagStack[i].fmark.fnum == fnum)
                col_adjust(&(port->tagStack[i].fmark.mark));
         } 

         //cursor position for other potals into the same buffer
         if (port != curPor)
            col_adjust(&port->cursor);
      }
   }
}

//When deleting lines, this may create duplicate marks in the jumplist. They will be removed here 
//for the specified ortal. When "loadfiles" is true first ensure entries have the "fnum" field set
//(this may be a bit slow).
pub void
cleanup_jumplist(Portal* wp, int loadfiles) {
   if (loadfiles) {
      //If specified, load all the files from the jump list. This is
      //needed to properly clean up duplicate entries, but will take some time.
      for (int i = 0; i < wp->jumpListLen; ++i) {
         if ((wp->jumpList[i].fmark.fnum == 0) && (wp->jumpList[i].fmark.mark.lnum != 0))
            fname2fnum(&wp->jumpList[i]);
      }
   }

   int to = 0;
   for (int from = 0; from < wp->jumpListLen; ++from) {
      if (wp->jumpListInd == from)
          wp->jumpListInd = to;
      int i = from + 1;
      for (; i < wp->jumpListLen; ++i) {
         if (wp->jumpList[i].fmark.fnum == wp->jumpList[from].fmark.fnum
                && wp->jumpList[from].fmark.fnum != 0
                && wp->jumpList[i].fmark.mark.lnum == wp->jumpList[from].fmark.mark.lnum)
            break;
      } 
      wp->jumpList[to++] = wp->jumpList[from];
   }
   if (wp->jumpListInd == wp->jumpListLen)
      wp->jumpListInd = to;
   wp->jumpListLen = to;
}

//Copy the jumplist from portal "from" to portal "to".
pub void
copy_jumplist(Portal* from, Portal* to) {
   for (int i = 0; i < from->jumpListLen; ++i) {
      to->jumpList[i] = from->jumpList[i];
      if (from->jumpList[i].fname != NULL)
          to->jumpList[i].fname = copyStr(from->jumpList[i].fname);
   }
   to->jumpListLen = from->jumpListLen;
   to->jumpListInd = from->jumpListInd;
}

//Free items in the jumplist of portal "wp".
pub void
free_jumplist(Portal *wp) {
   for (int i = 0; i < wp->jumpListLen; ++i)
      eeglFree(wp->jumpList[i].fname);
}

pub void
set_last_cursor(Portal *port) {
   if (port->book)
      port->book->lastCursor = port->cursor;
}

#if defined(EXITFREE)
pub void
free_all_marks(void) {
   int      i;

   for (i = 0; i < NMARKS + EXTRA_MARKS; i++) {
      if (namedfm[i].fmark.mark.lnum != 0)
          eeglFree(namedfm[i].fname);
   } 
}
#endif

//Return a pointer to the named file marks.
pub FileMarkExt *
get_namedfm(void) {
   return namedfm;
}

//Add information about mark 'mname' to list 'l'
private int
add_mark(List* l, CS mname, Pos* pos, int bufnr, CS fname) {
   if (pos->lnum <= 0)
      return OK;

   Bag* d = allocBag();
   if (!d)
      return FAIL;

   if (listAppendBag(l, d) == FAIL) {
      bagUnref(d);
      return FAIL;
   }

   List* lpos = list_alloc();

   list_append_number(lpos, bufnr);
   list_append_number(lpos, pos->lnum);
   list_append_number(lpos, pos->col < MAXCOL ? pos->col + 1 : MAXCOL);
   list_append_number(lpos, pos->coladd);

   if (bagAddString(d, S"mark", mname) == FAIL
          || bagAddList(d, S"pos", lpos) == FAIL
          || (fname != NULL && bagAddString(d, S"file", fname) == FAIL))
      return FAIL;

   return OK;
}

//Get information about marks local to a buffer.
private void
get_buf_local_marks(Book *book, List *l) {
   Byte   mname[3] = "' ";
   int      i;

   //Marks 'a' to 'z'
   for (i = 0; i < NMARKS; ++i) {
      mname[1] = 'a' + i;
      add_mark(l, mname, &book->namedMarks[i], book->fiNum, NULL);
   }

    //Mark '' is a portal local mark and not a buffer local mark
    add_mark(l, S"''", &curPor->prevContextMark, curBook->fiNum, NULL);

    add_mark(l, S"'\"", &book->lastCursor, book->fiNum, NULL);
    add_mark(l, S"'[", &book->opStart, book->fiNum, NULL);
    add_mark(l, S"']", &book->opEnd, book->fiNum, NULL);
    add_mark(l, S"'^", &book->lastInsert, book->fiNum, NULL);
    add_mark(l, S"'.", &book->lastChange, book->fiNum, NULL);
    add_mark(l, S"'<", &book->visual.vi_start, book->fiNum, NULL);
    add_mark(l, S"'>", &book->visual.vi_end, book->fiNum, NULL);
}

//Get information about global marks ('A' to 'Z' and '0' to '9')
private void
get_global_marks(List *l) {
   Byte   mname[3] = "' ";
   CS name;

   //Marks 'A' to 'Z' and '0' to '9'
   for (int i = 0; i < NMARKS + EXTRA_MARKS; ++i) {
      if (namedfm[i].fmark.fnum != 0)
         name = bookGetNameByBookNr(namedfm[i].fmark.fnum, true, true);
      else
         name = namedfm[i].fname;
      if (name) {
         mname[1] = i >= NMARKS ? i - NMARKS + '0' : i + 'A';
         add_mark(l, mname, &namedfm[i].fmark.mark, namedfm[i].fmark.fnum, name);
         if (namedfm[i].fmark.fnum != 0)
            eeglFree(name);
      }
   }
}

pub void
f_getmarklist(Arr(Var) argvars, Var* returnVar) {
   allocReturnList(returnVar);

   if (argvars[0].tag == VAR_UNKNOWN) {
      get_global_marks(returnVar->list);
      return;
   }

   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   get_buf_local_marks(book, returnVar->list);
}

//}}}
//{{{signs (little chars shown in the leftmost column, like changed line markings)

//Iterate through all the signs placed in a book
#define FOR_ALL_SIGNS_IN_BOOK(book, sign) \
    for ((sign) = (book)->signList; (sign) != NULL; (sign) = (sign)->next)


//Struct to hold the sign properties.

struct Sign {
   Sign* next; //next sign in list
   int typeNr; //type number of sign
   CS name; //name of sign
   CS text; //text used instead of pixmap
   Short lineHiId; //hilite ID for line
   Short textHiId; //hilite ID for text
   Short cursorLineHiId; //hilite ID for text on current line when 'cursorline' is set
   Short lineNumHiId; //hilite ID for line number
   int priority; //default priority of this sign, -1 means SIGN_DEF_PRIO
};

private Sign *first_sign = NULL;
private int next_sign_typenr = 1;

private void sign_list_defined(Sign *sp);
private void sign_undefine(Sign *sp, Sign *sp_prev);

private CS cmds[] = {SMAP((CS),
   "define",
# define SIGNCMD_DEFINE 0
   "undefine",
# define SIGNCMD_UNDEFINE 1
   "list",
# define SIGNCMD_LIST 2
   "place",
# define SIGNCMD_PLACE 3
   "unplace",
# define SIGNCMD_UNPLACE 4
   "jump",
# define SIGNCMD_JUMP 5
   NULL
# define SIGNCMD_LAST 6
)};

# define FOR_ALL_SIGNS(sp) \
     for ((sp) = first_sign; (sp); (sp) = (sp)->next)

private EeSet signGroups; //sign group (SignGroup) hashtable
private int next_sign_id = 1; //next sign id in the global group

//Initialize data needed for managing signs
pub void
init_signs(void) {
   hash_init(&signGroups); //sign group hash table
}

//Macros to get the sign group structure from the group name
#define SGN_KEY_OFF   offsetof(SignGroup, sg_name)
#define HI2SG(hi)   ((SignGroup *)((hi)->hi_key - SGN_KEY_OFF))

//A new sign in group 'groupname' is added. If the group is not present,
//create it. Otherwise reference the group.
private SignGroup *
sign_group_ref(CS groupname) {
   Text groupName = mbText(groupname);
   Hash hash = calcHash(groupName);
   EeSetItem *hi = hash_lookup(&signGroups, groupName, hash);
   SignGroup* group = NULL;

   if (HASHITEM_EMPTY(hi)) {
      //new group
      group = alloc(offsetof(SignGroup, sg_name) + STRLEN(groupname) + 1);

      STRCPY(group->sg_name, groupname);
      group->sg_refcount = 1;
      group->sg_next_sign_id = 1;
      group->isPopupOnly = STRNCMP("PopUp", groupname, 5) == 0;
      hash_add_item(&signGroups, hi, mbText(group->sg_name), hash);
   } else {
      //existing group
      group = HI2SG(hi);
      group->sg_refcount++;
   }

   return group;
}

//A sign in group 'groupname' is removed. If all the signs in this group are
//removed, then remove the group.
private void
sign_group_unref(CS groupname) {
   EeSetItem *hi = hash_find(&signGroups, mbText(groupname));
   if (HASHITEM_EMPTY(hi))
      return;

   SignGroup* group = HI2SG(hi);
   group->sg_refcount--;
   if (group->sg_refcount == 0) {
       //All the signs in this group are removed
       hash_remove(&signGroups, hi, S"sign remove");
       eeglFree(group);
   }
}

//Return true if 'sign' is in 'group'.
//A sign can either be in the global group (sign->group == NULL)
//or in a named group. If 'group' is '*', then the sign is part of the group.
private int
sign_in_group(SignEntry *sign, CS group) {
   return ((group && STRCMP(group, "*") == 0) 
          || (!group && !sign->group) 
          || (group && sign->group && STRCMP(group, sign->group->sg_name) == 0)
   );
}

//Return true if "sign" is to be displayed in portal "wp".
//If the group name starts with "PopUp", it only shows in a popup portal.
private Boole
signIsVisible(SignEntry* sign, Portal* po) {
   Boole for_popup = sign->group && sign->group->isPopupOnly;
   return PORTAL_IS_POPUP(po) ? for_popup : !for_popup;
}

//Get the next free sign identifier in the specified group
private int
sign_group_get_next_signid(Book *book, CS groupname) {
   int id = 1;
   SignGroup *group = NULL;
   SignEntry *sign = NULL;

   if (groupname) {
      EeSetItem *hi = hash_find(&signGroups, text(groupname));
      if (HASHITEM_EMPTY(hi))
         return id;
      group = HI2SG(hi);
   }

   //Search for the next usable sign identifier
   Boole found = false;
   while (!found) {
      if (group) {
         id = group->sg_next_sign_id;
         group->sg_next_sign_id++;
      } else {
         id = next_sign_id; //global group
         next_sign_id++;
      } 

      //Check whether this sign is already placed in the buffer
      found = found;
      FOR_ALL_SIGNS_IN_BOOK(book, sign) {
         if (id == sign->id && sign_in_group(sign, groupname)) {
            found = false; //sign identifier is in use
            break;
         }
      }
   }

   return id;
}

//Insert a new sign into the signlist for buffer 'book' between the 'prev' and 'next' signs.
private void
insert_sign(
   Book *book, //buffer to store sign in
   SignEntry* prev, //previous sign entry
   SignEntry* next, //next sign entry
   int id, //sign ID
   CS group, //sign group; NULL for global group
   int prio, //sign priority
   LineNr lnum, //line number which gets the mark
   int typenr
) {//typenr of sign we are adding
   SignEntry *newsign = lalloc_id(sizeof(SignEntry), false, aid_insert_sign);
   if (!newsign)
      return;

   newsign->id = id;
   newsign->lnum = lnum;
   newsign->typeNr = typenr;

   if (group) {
      newsign->group = sign_group_ref(group);
      if (!newsign->group) {
         eeglFree(newsign);
         return;
      }
   } else {
      newsign->group = NULL;
   }

   newsign->priority = prio;
   newsign->next = next;
   newsign->prev = prev;
   if (next)
      next->prev = newsign;

   if (!prev) {
      //When adding first sign need to redraw the windows to create the column for signs.
      if (!book->signList) {
         drawBookLater(book, UPD_NOT_VALID);
         changed_line_abv_curs();
      }

      //first sign in signlist
      book->signList = newsign;
   } else {
      prev->next = newsign;
   }
}

//Insert a new sign sorted by line number and sign priority.
private void
insert_sign_by_lnum_prio(
   Book *book, //buffer to store sign in
   SignEntry *prev, //previous sign entry
   int id, //sign ID
   Byte *group, //sign group; NULL for global group
   int prio, //sign priority
   LineNr lnum, //line number which gets the mark
   int typenr
) {   //typenr of sign we are adding
      //keep signs sorted by lnum and by priority: insert new sign at
      //the proper position in the list for this lnum.
      while (prev && prev->lnum == lnum && prev->priority <= prio)
         prev = prev->prev;

      SignEntry *sign = (!prev) ? book->signList : prev->next;

      insert_sign(book, prev, sign, id, group, prio, lnum, typenr);
}

//Lookup a sign by typenr. Returns NULL if sign is not found.
private Sign *
find_sign_by_typenr(int typenr) {
   Sign *sp = NULL;
   FOR_ALL_SIGNS(sp) {
      if (sp->typeNr == typenr)
         return sp;
   } 
   return NULL;
}

//Get the name of a sign by its typenr.
private CS
sign_typenr2name(int typenr) {
    Sign *sp = NULL;
    FOR_ALL_SIGNS(sp) {
       if (sp->typeNr == typenr)
          return sp->name;
    } 
    return (CS)_("[Deleted]");
}

//Return information about a sign in a Bag
private Bag*
sign_get_info(SignEntry* sign) {
   Bag* b = allocBag_id(aid_sign_getinfo);
   if (!b)
      return NULL;

   bagAddNumber(b, S"id", sign->id);
   bagAddString(b, S"group", sign->group ?sign->group->sg_name : S"");
   bagAddNumber(b, S"lnum", sign->lnum);
   bagAddString(b, S"name", sign_typenr2name(sign->typeNr));
   bagAddNumber(b, S"priority", sign->priority);

   return b;
}

//Sort the signs placed on the same line as "sign" by priority.  Invoked after
//changing the priority of an already placed sign.  Assumes the signs in the
//buffer are sorted by line number and priority.
private void
sign_sort_by_prio_on_line(Book *book, SignEntry *sign) {
    //If there is only one sign in the buffer or only one sign on the line or
    //the sign is already sorted by priority, then return.
    if ((sign->prev == NULL || sign->prev->lnum != sign->lnum ||
         sign->prev->priority > sign->priority) &&
        (sign->next == NULL || sign->next->lnum != sign->lnum ||
         sign->next->priority < sign->priority))
        return;

    //One or more signs on the same line as 'sign'
    //Find a sign after which 'sign' should be inserted

    //First search backward for a sign with higher priority on the same line
    SignEntry *p = sign;
    while (p->prev && p->prev->lnum == sign->lnum &&
           p->prev->priority <= sign->priority)
       p = p->prev;

    if (p == sign) {
        //Sign not found. Search forward for a sign with priority just before
        //'sign'.
        p = sign->next;
        while (p->next != NULL && p->next->lnum == sign->lnum &&
               p->next->priority > sign->priority)
           p = p->next;
    }

    //Remove 'sign' from the list
    if (book->signList == sign)
        book->signList = sign->next;

    if (sign->prev)
        sign->prev->next = sign->next;

    if (sign->next != NULL)
        sign->next->prev = sign->prev;

    sign->prev = NULL;
    sign->next = NULL;

    //Re-insert 'sign' at the right place
    if (p->priority <= sign->priority) {
        //'sign' has a higher priority and should be inserted before 'p'
        sign->prev = p->prev;
        sign->next = p;
        p->prev = sign;
        if (sign->prev)
            sign->prev->next = sign;

        if (book->signList == p)
            book->signList = sign;
    } else {
        //'sign' has a lower priority and should be inserted after 'p'
        sign->prev = p;
        sign->next = p->next;
        p->next = sign;
        if (sign->next)
            sign->next->prev = sign;
    }
}

//Add the sign into the signlist. Find the right spot to do it though.
private void
addSignToBook(
   Book* book, //book to store sign in
   int id, //sign ID
   CS groupname, //sign group
   int prio, //sign priority
   LineNr lnum, //line number which gets the mark
   int typenr //typenr of sign we are adding
){
   SignEntry *sign = NULL; //a sign in the signlist
   SignEntry *prev = NULL; //the previous sign
   FOR_ALL_SIGNS_IN_BOOK(book, sign) {
      if (lnum == sign->lnum && id == sign->id && sign_in_group(sign, groupname)) {
          //Update an existing sign
          sign->typeNr = typenr;
          sign->priority = prio;
          sign_sort_by_prio_on_line(book, sign);
          return;
      } ei (lnum < sign->lnum) {
          insert_sign_by_lnum_prio(book, prev, id, groupname, prio, lnum, typenr);
            return;
      }
      prev = sign;
   }

   insert_sign_by_lnum_prio(book, prev, id, groupname, prio, lnum, typenr);
}

//For an existing, placed sign "markId" change the type to "typenr".
//Return the line number of the sign, or zero if the sign is not found.
private LineNr
changeSignType(
   Book* book, //book to store sign in
   int markId, //sign ID
   CS group, //sign group
   int typenr, //typenr of sign we are adding
   int prio //sign priority
){
    SignEntry *sign = NULL; //a sign in the signlist
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
        if (sign->id == markId && sign_in_group(sign, group)) {
            sign->typeNr = typenr;
            sign->priority = prio;
            sign_sort_by_prio_on_line(book, sign);
            return sign->lnum;
        }
    }

    return (LineNr)0;
}

//Return the decorations of the first sign placed on line 'lnum' in buffer 'buf'. Used when 
//refreshing the screen. Returns true if a sign is found on 'lnum', false otherwise.
pub int
markGetSignDecorations(Portal *wp, LineNr lnum, OUT SignHilite* signHilites) {
   CLEAR_POINTER(signHilites);

   Book* buf = wp->book;
   SignEntry* sign = NULL;
   FOR_ALL_SIGNS_IN_BOOK(buf, sign) {
      //Signs are sorted by line number in the buffer. No need to check
      //for signs after the specified line number 'lnum'.
      if (sign->lnum > lnum)
          break;

      if (sign->lnum == lnum && signIsVisible(sign, wp)) {
         signHilites->typeNr = sign->typeNr;
         Sign *sp = find_sign_by_typenr(sign->typeNr);
         if (!sp)
            return false;

         signHilites->text = sp->text;

         if (signHilites->text != NULL && sp->textHiId > 0)
            signHilites->textHiId = decorationByHiliteId(sp->textHiId);

         if (sp->lineHiId < SHORT)
            signHilites->lineHiId = decorationByHiliteId(sp->lineHiId);

         if (sp->cursorLineHiId < SHORT)
            signHilites->cursorLineHiId = sp->cursorLineHiId;

         if (sp->lineNumHiId < SHORT)
            signHilites->lineNumHiId = sp->lineNumHiId;

         signHilites->priority = sign->priority;

         //If there is another sign next with the same priority, may
         //combine the text and the line highlighting.
         if (sign->next != NULL &&
              sign->next->priority == sign->priority &&
              sign->next->lnum == sign->lnum
         ) {
            Sign *next_sp = find_sign_by_typenr(sign->next->typeNr);
            if (!next_sp)
               return false;

            if (!signHilites->icon && !signHilites->text) {
               signHilites->text = next_sp->text;
            }

            if (sp->textHiId == SHORT && next_sp->textHiId < SHORT)
               signHilites->textHiId = next_sp->textHiId;

            if (sp->lineHiId == SHORT && next_sp->lineHiId < SHORT)
               signHilites->lineHiId = next_sp->lineHiId;

            if (sp->cursorLineHiId == SHORT && next_sp->cursorLineHiId < SHORT)
               signHilites->cursorLineHiId = next_sp->cursorLineHiId;

            if (sp->lineNumHiId == SHORT && next_sp->lineNumHiId < SHORT)
               signHilites->lineNumHiId = next_sp->lineNumHiId;
         }
         return true;
      }
   }
   return false;
}

//Delete sign 'id' in group 'group' from buffer 'buf'.
//If 'id' is zero, then delete all the signs in group 'group'. Otherwise
//delete only the specified sign.
//If 'group' is '*', then delete the sign in all the groups. If 'group' is
//NULL, then delete the sign in the global group. Otherwise delete the sign in
//the specified group.
//Return the line number of the deleted sign. If multiple signs are deleted,
//then returns the line number of the last sign deleted.
private LineNr
delsign(Book* book, //buffer sign is stored in
            LineNr atlnum, //sign at this line, 0 - at any line
            int id, //sign id
            Byte *group) //sign group
{
   //pointer to pointer to current sign
   SignEntry **lastp = &book->signList;
   SignEntry *next = NULL; //the next sign in a signList
   LineNr lnum = 0; //line number whose sign was deleted

   for (SignEntry *sign = book->signList; sign != NULL; sign = next) {
      next = sign->next;

      if ((id == 0 || sign->id == id) &&
            (atlnum == 0 || sign->lnum == atlnum) && sign_in_group(sign, group)
      ) {
          *lastp = next;
          if (next)
             next->prev = sign->prev;

          lnum = sign->lnum;

          if (sign->group)
             sign_group_unref(sign->group->sg_name);

          eeglFree(sign);
          drawBookLineLater(book, lnum);

            //Check whether only one sign needs to be deleted
            //If deleting a sign with a specific identifier in a particular
            //group or deleting any sign at a particular line number, delete
            //only one sign.
            if (!group || (*group != '*' && id != 0) ||
                (*group == '*' && atlnum != 0))
                break;
        } else {
            lastp = &sign->next;
        }
    }

    //When deleting the last sign the cursor position may change, because the
    //sign columns no longer shows.  And the 'signcolumn' may be hidden.
    if (book->signList == NULL) {
        drawBookLater(book, UPD_NOT_VALID);
        changed_line_abv_curs();
    }

    return lnum;
}

//Find the line number of the sign with the requested id in group 'group'. If
//the sign does not exist, return 0 as the line number. This will still let
//the correct file get loaded.
private int
buf_findsign(Book *book, //buffer to store sign in
             int id, //sign ID
             CS group) //sign group
{
    SignEntry *sign = NULL; //a sign in the signlist
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
       if (sign->id == id && sign_in_group(sign, group))
          return sign->lnum;
    } 

    return 0;
}

//Return the sign at line 'lnum' in book. Return NULL if a sign is
//not found at the line. If 'groupname' is NULL, search in the global group.
private SignEntry *
getsignAtLine(Book* book, //book whose sign we are searching for
              LineNr lnum, //line number of sign
              CS groupname //sign group name
){
   SignEntry *sign = NULL; //a sign in the signlist
   FOR_ALL_SIGNS_IN_BOOK(book, sign) {
       //Signs are sorted by line number in the book. No need to check
       //for signs after the specified line number 'lnum'.
       if (sign->lnum > lnum)
          break;

       if (sign->lnum == lnum && sign_in_group(sign, groupname))
          return sign;
   }

   return NULL;
}

//Return the identifier of the sign at line number 'lnum' in book.
private int
findsign_id(Book* book, //book whose sign we are searching for
            LineNr lnum, //line number of sign
            CS groupname //sign group name
){
    //a sign in the signlist
    SignEntry *sign = getsignAtLine(book, lnum, groupname);
    if (sign != NULL)
        return sign->id;

    return 0;
}


//Delete signs in group 'group' in book. If 'group' is '*', then delete all the signs.
pub void
llDeleteSigns(Book* book, CS group) {
    //When deleting the last sign need to redraw the windows to remove the
    //sign column. Not when curPor is NULL (this means we're exiting).
    if (book->signList && curPor) {
        drawBookLater(book, UPD_NOT_VALID);
        changed_line_abv_curs();
    }

    //pointer to pointer to current sign
    SignEntry **lastp = &book->signList;
    SignEntry *next = NULL;

    for (SignEntry *sign = book->signList; sign != NULL; sign = next) {
        next = sign->next;
        if (sign_in_group(sign, group)) {
            *lastp = next;

            if (next)
               next->prev = sign->prev;

            if (sign->group)
               sign_group_unref(sign->group->sg_name);

            eeglFree(sign);
        } else {
           lastp = &sign->next;
        }
    }
}

//List placed signs for "rbook".  If "rbook" is NULL do it for all books.
private void
sign_list_placed(Book* rbook, CS sign_group) {
   Byte lbuf[MSG_BUF_LEN];
   Byte group[MSG_BUF_LEN];

   msg_puts_title(_("\n--- Signs ---"));
   msg_putchar('\n');

   Book* book = (!rbook) ? firstBook : rbook;
   while (book && !gotInterruptG) {
     if (book->signList != NULL) {
         eeSnprintf(lbuf, MSG_BUF_LEN, _("Signs for %s:"), book->currFileName);
         msgPutsDeco(lbuf, getDecoFlags(HLF_D));
         msg_putchar('\n');
      }

      SignEntry *sign = NULL;
      FOR_ALL_SIGNS_IN_BOOK(book, sign) {
         if (gotInterruptG)
            break;

         if (!sign_in_group(sign, sign_group))
            continue;

         if (sign->group)
            eeSnprintf(group, MSG_BUF_LEN, _("  group=%s"), sign->group->sg_name);
         else
            group[0] = '\0';

         eeSnprintf(lbuf, MSG_BUF_LEN,
                      _("    line=%ld  id=%d%s  name=%s  priority=%d"),
                      (long)sign->lnum, sign->id, group,
                      sign_typenr2name(sign->typeNr), sign->priority);

         msg_puts(lbuf);
         msg_putchar('\n');
      }

      if (rbook)
         break;

      book = book->next;
   }
}

//Adjust a placed sign for inserted/deleted lines.
private void
sign_mark_adjust(
    LineNr line1,
    LineNr line2,
    long amount,
    long amount_after
) {
   SignEntry *sign = NULL; //a sign in a signList
   FOR_ALL_SIGNS_IN_BOOK(curBook, sign) {
      //Ignore changes to lines after the sign
      if (sign->lnum < line1)
         continue;

      LineNr new_lnum = sign->lnum;

      if (sign->lnum <= line2) {
         if (amount != MAXLNUM)
            new_lnum += amount;
      } ei (sign->lnum > line2) {
         //Lines inserted or deleted before the sign
         new_lnum += amount_after;
      }

      //If the new sign line number is past the last line in the book,
      //then don't adjust the line number. Otherwise, it will always be past
      //the last line and will not be visible.
      if (new_lnum <= curBook->mem.lineCount)
         sign->lnum = new_lnum;
   }
}

//Find index of a ":sign" subcmd from its name. "*end_cmd" must be writable.
private int
sign_cmd_idx(CS begin_cmd, //begin of sign subcmd
             CS end_cmd //just after sign subcmd
){
   int idx = 0;
   Byte save = *end_cmd;
   *end_cmd = ZERO;

   while (cmds[idx] != NULL && STRCMP(begin_cmd, cmds[idx]) != 0)
      ++idx;

   *end_cmd = save;
   return idx;
}

//Find a sign by name. Also return pointer to the previous sign.
private Sign*
sign_find(CS name, Sign** sp_prev) {
   if (sp_prev)
      *sp_prev = NULL;

   Sign *sp = NULL;
   FOR_ALL_SIGNS(sp) {
      if (STRCMP(sp->name, name) == 0)
         break;

      if (sp_prev != NULL)
         *sp_prev = sp;
   }

   return sp;
}

//Allocate a new sign
private Sign *
alloc_new_sign(CS name) {
   int start = next_sign_typenr;

   //Allocate a new sign.
   Sign *sp = allocZeroed_id(sizeof(Sign), aid_sign_define_by_name);

   //Check that next_sign_typenr is not already being used.
   //This only happens after wrapping around. Hopefully
   //another one got deleted and we can use its number.
   Sign *lp = first_sign;
   while (lp != NULL) {
       if (lp->typeNr == next_sign_typenr) {
           ++next_sign_typenr;

           if (next_sign_typenr == MAX_TYPENR)
              next_sign_typenr = 1;

           if (next_sign_typenr == start) {
              eeglFree(sp);
              emsg(_(e_too_many_signs_defined));
              return NULL;
           }

           lp = first_sign; //start all over
           continue;
       }
       lp = lp->next;
   }

   sp->typeNr = next_sign_typenr;

   if (++next_sign_typenr == MAX_TYPENR)
      next_sign_typenr = 1; //wrap around

   sp->name = copyStr(name);

   return sp;
}

//Initialize the text for a new sign
private int
sign_define_init_text(Sign *sp, Byte *text) {
   Byte *s = NULL;
   Byte *endp = text + (int)STRLEN(text);
   int cells = 0;

   //Remove backslashes so that it is possible to use a space.
   for (s = text; s + 1 < endp; ++s) {
      if (*s == '\\') {
          STRMOVE(s, s + 1);
          --endp;
      }
   }

   //Count cells and check for non-printable chars
   for (s = text; s < endp; s += utfCharLen(s)) {
      if (!bookIsCharPrintable((*mb_ptr2char)(s)))
         break;
      cells += (*mb_ptr2cells)(s);
   }

   //Currently sign text must be one or two display cells
   if (s != endp || cells < 1 || cells > 2) {
      showErrFmtMsg(_(e_invalid_sign_text_str), text);
      return FAIL;
   }

   eeglFree(sp->text);
   //Allocate one byte more if we need to pad up with a space.
   int len = (int)(endp - text + ((cells == 1) ? 1 : 0));
   sp->text = copySubstr(text, len);

   //For single character sign text, pad with a space.
   if (sp->text && cells == 1)
      STRCPY(sp->text + len - 1, " ");

   return OK;
}

//Define a new sign or update an existing sign
pub int
sign_define_by_name(
   CS name,
   CS linehl,
   CS textt,
   CS texthl,
   CS culhl,
   CS numhl,
   int prio
){
   Sign *sp_prev = NULL;
   Sign *sp = sign_find(name, &sp_prev);
   if (!sp) {
      sp = alloc_new_sign(name);
      //add the new sign to the list of signs
      if (!sp_prev)
         first_sign = sp;
      else
         sp_prev->next = sp;
   } else {
       Portal *wp = NULL;
       //Signs may already exist, a redraw is needed in windows with a
       //non-empty sign list.
       FOR_ALL_PORTALS(wp) {
          if (wp->book->signList != NULL)
              drawBookLater(wp->book, UPD_NOT_VALID);
       }
   }

   //set values for a defined sign.

   if (textt && (sign_define_init_text(sp, textt) == FAIL))
      return FAIL;

   sp->priority = prio;

   if (linehl) {
      if (*linehl == ZERO)
         sp->lineHiId = 0;
      else
         sp->lineHiId = hiliteGroupByName(text(linehl));
   }

   if (texthl) {
      if (*texthl == ZERO)
         sp->textHiId = 0;
      else
         sp->textHiId = hiliteGroupByName(text(texthl));
   }

   if (culhl) {
      if (*culhl == ZERO)
         sp->cursorLineHiId = 0;
      else
         sp->cursorLineHiId = hiliteGroupByName(text(culhl));
   }

   if (numhl) {
      if (*numhl == ZERO)
         sp->lineNumHiId = 0;
      else
         sp->lineNumHiId = hiliteGroupByName(text(numhl));
   }

   return OK;
}

//Return true if sign "name" exists.
pub int
sign_exists_by_name(CS name) {
   return sign_find(name, NULL) != NULL;
}

//Free the sign specified by 'name'.
pub int
sign_undefine_by_name(CS name, Boole give_error) {
   Sign *sp_prev = NULL;
   Sign *sp = sign_find(name, &sp_prev);
   if (!sp) {
      if (give_error)
         showErrFmtMsg(_(e_unknown_sign_str), name);
      return FAIL;
   }
   sign_undefine(sp, sp_prev);

   return OK;
}

//List the signs matching 'name'
private void
sign_list_by_name(Byte *name) {
   Sign *sp = sign_find(name, NULL);
   if (sp != NULL)
      sign_list_defined(sp);
   else
      showErrFmtMsg(_(e_unknown_sign_str), name);
}

private void
may_force_numberwidth_recompute(Book* book, int unplace) {
   Tab *t;
   Portal *wp;
   FOR_ALL_TAB_PORTALS(t, wp) {
      if (wp->book == book && (unplace || wp->lineCountSaved < 2) && wp->o.signColumn)
         wp->lineCountSaved = 0;
   }
}

//Place a sign at the specified file location or update a sign.
pub int
sign_place(
   int *sign_id,
   CS sign_group,
   CS sign_name,
   Book* book,
   LineNr lnum,
   int prio
) {
   //Check for reserved character '*' in group name
   if (sign_group != NULL && (*sign_group == '*' || *sign_group == '\0'))
      return FAIL;

   Sign *sp = NULL;
   FOR_ALL_SIGNS(sp) {
      if (STRCMP(sp->name, sign_name) == 0)
         break;
   }

   if (!sp) {
      showErrFmtMsg(_(e_unknown_sign_str), sign_name);
      return FAIL;
   }

   if (*sign_id == 0)
      *sign_id = sign_group_get_next_signid(book, sign_group);

   //Use the default priority value for this sign.
   if (prio == -1)
      prio = (sp->priority != -1) ? sp->priority : SIGN_DEF_PRIO;

   if (lnum > 0) {
      //":sign place {id} line={lnum} name={name} file={fname}": place a sign
      addSignToBook(book, *sign_id, sign_group, prio, lnum, sp->typeNr);
   } else {
      //":sign place {id} file={fname}": change sign type and/or priority
      lnum = changeSignType(book, *sign_id, sign_group, sp->typeNr, prio);
   }

    if (lnum > 0) {
       drawBookLineLater(book, lnum);

       //When displaying signs in the 'number' column, if the width of the
       //number column is less than 2, then force recomputing the width.
       may_force_numberwidth_recompute(book, false);
    } else {
       showErrFmtMsg(_(e_not_possible_to_change_sign_str), sign_name);
       return FAIL;
    }

    return OK;
}

//Unplace the specified sign
private int
sign_unplace(int sign_id, Byte *sign_group, Book* book, LineNr atlnum) {
   if (!book->signList) //No signs in the book
      return OK;

   if (sign_id == 0) {
      //Delete all the signs in the specified book
      drawBookLater(book, UPD_NOT_VALID);
      llDeleteSigns(book, sign_group);
   } else {
      //Delete only the specified signs
      LineNr lnum = delsign(book, atlnum, sign_id, sign_group);
      if (lnum == 0)
         return FAIL;
   }

   //When all the signs in a book are removed, force recomputing the number column width 
   //(if enabled) in all the portals into the book if @signcolumn is set to 'number' in that portal
   if (book->signList == NULL)
      may_force_numberwidth_recompute(book, true);

   return OK;
}

//Unplace the sign at the current cursor line.
private void
sign_unplace_at_cursor(CS groupname) {
    int id = findsign_id(curPor->book, curPor->cursor.lnum, groupname);
    if (id > 0)
       sign_unplace(id, groupname, curPor->book, curPor->cursor.lnum);
    else
       emsg(_(e_missing_sign_number));
}

// Jump to a sign.
private LineNr
sign_jump(int sign_id, Byte *sign_group, Book* book) {
    LineNr lnum = buf_findsign(book, sign_id, sign_group);
    if (lnum <= 0) {
       showErrFmtMsg(_(e_invalid_sign_id_nr), sign_id);
       return -1;
    }

    //goto a sign ...
    if (portTryFindOpenBook(book) != NULL) { //... in a current portal
       curPor->cursor.lnum = lnum;
       check_cursor_lnum();
       beginline(BL_WHITE);
    } else { //... not currently in a portal
        if (book->currFileName == NULL) {
           emsg(_(e_cannot_jump_to_buffer_that_does_not_have_name));
           return -1;
        }
        CS cmd = alloc(STRLEN(book->currFileName) + 25);

        sprintf((char *)cmd, "e +%ld %s", (long)lnum, book->currFileName);
        executeCommLine(cmd);
        eeglFree(cmd);
    }
    foldOpenCursor();

    return lnum;
}

//":sign define {name} ..." command
private void
sign_define_cmd(Byte *sign_name, Byte *cmdline) {
   CS arg = NULL;
   CS p = cmdline;
   CS text = NULL;
   CS linehl = NULL;
   CS texthl = NULL;
   CS culhl = NULL;
   CS numhl = NULL;
   int prio = -1;
   int failed = false;

   //set values for a defined sign.
   while (true) {
      arg = skipwhite(p);
      if (*arg == ZERO)
          break;

      p = skiptowhite_esc(arg);
      if (STRNCMP(arg, "text=", 5) == 0) {
          arg += 5;
          text = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "linehl=", 7) == 0) {
          arg += 7;
          linehl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "texthl=", 7) == 0) {
          arg += 7;
          texthl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "culhl=", 6) == 0) {
          arg += 6;
          culhl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "numhl=", 6) == 0) {
          arg += 6;
          numhl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "priority=", 9) == 0) {
          arg += 9;
          prio = atoi((char *)arg);
      } else {
          showErrFmtMsg(_(e_invalid_argument_str), arg);
          failed = true;
          break;
      }
   }

   if (!failed)
      sign_define_by_name(sign_name, linehl, text, texthl, culhl, numhl, prio);

   eeglFree(text);
   eeglFree(linehl);
   eeglFree(texthl);
   eeglFree(culhl);
   eeglFree(numhl);
}

//":sign place" command
private void
sign_place_cmd(
   Book* book,
   LineNr lnum,
   CS sign_name,
   int id,
   CS group,
   int prio
) {
   if (id <= 0) {
        //List signs placed in a file/buffer
        //  :sign place file={fname}
        //  :sign place group={group} file={fname}
        //  :sign place group=* file={fname}
        //  :sign place buffer={nr}
        //  :sign place group={group} buffer={nr}
        //  :sign place group=* buffer={nr}
        //  :sign place
        //  :sign place group={group}
        //  :sign place group=*
        if (lnum >= 0 || sign_name != NULL || (group != NULL && *group == '\0'))
            emsg(_(e_invalid_argument));
        else
            sign_list_placed(book, group);
   } else {
      //Place a new sign
      if (sign_name == NULL || !book || (group && *group == '\0')) {
          emsg(_(e_invalid_argument));
          return;
      }

      sign_place(&id, group, sign_name, book, lnum, prio);
   }
}

//":sign unplace" command
private void
sign_unplace_cmd(Book* book, LineNr lnum, CS sign_name, int id, CS group) {
   if (lnum >= 0 || sign_name != NULL || (group != NULL && *group == '\0')) {
       emsg(_(e_invalid_argument));
       return;
   }

   if (id == -2) {
       if (book) {
            //:sign unplace * file={fname}
            //:sign unplace * group={group} file={fname}
            //:sign unplace * group=* file={fname}
            //:sign unplace * buffer={nr}
            //:sign unplace * group={group} buffer={nr}
            //:sign unplace * group=* buffer={nr}
            sign_unplace(0, group, book, 0);
        } else {
            //:sign unplace *
            //:sign unplace * group={group}
            //:sign unplace * group=*
            FOR_ALL_BOOKS(book) {
               if (book->signList)
                   llDeleteSigns(book, group);
            }
        }
    } else {
        if (book) {
            //:sign unplace {id} file={fname}
            //:sign unplace {id} group={group} file={fname}
            //:sign unplace {id} group=* file={fname}
            //:sign unplace {id} buffer={nr}
            //:sign unplace {id} group={group} buffer={nr}
            //:sign unplace {id} group=* buffer={nr}
            sign_unplace(id, group, book, 0);
        } else {
            if (id == -1) {
                //:sign unplace group={group}
                //:sign unplace group=*
                sign_unplace_at_cursor(group);
            } else {
                //:sign unplace {id}
                //:sign unplace {id} group={group}
                //:sign unplace {id} group=*
                FOR_ALL_BOOKS(book)
                    sign_unplace(id, group, book, 0);
            }
        }
    }
}

//Jump to a placed sign commands:
// :sign jump {id} file={fname}
// :sign jump {id} buffer={nr}
// :sign jump {id} group={group} file={fname}
// :sign jump {id} group={group} buffer={nr}
private void
sign_jump_cmd(
   Book* book,
   LineNr lnum,
   CS sign_name,
   int id,
   CS group
) {
    if (!sign_name && !group && id == -1) {
        emsg(_(e_argument_required));
        return;
    }

    if (!book || (group && *group == ZERO) || lnum >= 0 || sign_name) {
        //File or book is not specified or an empty group is used
        //or a line number or a sign name is specified.
        emsg(_(e_invalid_argument));
        return;
    }

    (void)sign_jump(id, group, book);
}

//Parse the command line arguments for the ":sign place", ":sign unplace" and
//":sign jump" commands.
//The supported arguments are: line={lnum} name={name} group={group}
//priority={prio} and file={fname} or buffer={nr}.
private int
parse_sign_cmd_args(
   int cmd,
   CS arg,
   OUT CS* sign_name,
   int* signid,
   Byte** group,
   int *prio,
   Book** book,
   LineNr* lnum
) {
    Byte *arg1 = arg;
    Byte *filename = NULL;
    int lnum_arg = false;

    //first arg could be placed sign id
    if (EE_ISDIGIT(*arg)) {
        *signid = parseLong(&arg);
        if (!SPACE_OR_TAB(*arg) && *arg != ZERO) {
            *signid = -1;
            arg = arg1;
        } else {
            arg = skipwhite(arg);
        }
    }

    while (*arg != ZERO) {
        if (STRNCMP(arg, "line=", 5) == 0) {
            arg += 5;
            *lnum = atoi((char *)arg);
            arg = skiptowhite(arg);
            lnum_arg = true;
        } ei (STRNCMP(arg, "*", 1) == 0 && cmd == SIGNCMD_UNPLACE) {
            if (*signid != -1) {
                emsg(_(e_invalid_argument));
                return FAIL;
            }
            *signid = -2;
            arg = skiptowhite(arg + 1);
        } ei (STRNCMP(arg, "name=", 5) == 0) {
            arg += 5;
            Byte *name = arg;
            arg = skiptowhite(arg);
            if (*arg != ZERO)
                *arg++ = ZERO;

            while (name[0] == '0' && name[1] != ZERO)
                ++name;

            *sign_name = name;
        } ei (STRNCMP(arg, "group=", 6) == 0) {
            arg += 6;
            *group = arg;
            arg = skiptowhite(arg);
            if (*arg != ZERO)
                *arg++ = ZERO;
        } ei (STRNCMP(arg, "priority=", 9) == 0) {
            arg += 9;
            *prio = atoi((char *)arg);
            arg = skiptowhite(arg);
        } ei (STRNCMP(arg, "file=", 5) == 0) {
            arg += 5;
            filename = arg;
            *book = booklistFindByNameExpandingLinks(arg);
            break;
        } ei (STRNCMP(arg, "buffer=", 7) == 0) {
            arg += 7;
            filename = arg;
            *book = bookFindFileByBookNr((int)parseLong(&arg));

            if (*skipwhite(arg) != ZERO)
                showErrFmtMsg(_(e_trailing_characters_str), arg);

            break;
        } else {
            emsg(_(e_invalid_argument));
            return FAIL;
        }

        arg = skipwhite(arg);
    }

    if (filename != NULL && *book == NULL) {
       showErrFmtMsg(_(e_invalid_buffer_name_str), filename);
       return FAIL;
    }

    //If the filename is not supplied for the sign place or the sign jump
    //command, then use the current book.
    if (filename == NULL &&
       ((cmd == SIGNCMD_PLACE && lnum_arg) || cmd == SIGNCMD_JUMP))
       *book = curPor->book;

    return OK;
}

pub void
c_sign(Invocation* invo) {
    CS arg = invo->arg;

    //Parse the subcommand.
    CS p = skiptowhite(arg);
    int idx = sign_cmd_idx(arg, p);
    if (idx == SIGNCMD_LAST) {
       showErrFmtMsg(_(e_unknown_sign_command_str), arg);
       return;
    }
    arg = skipwhite(p);

    if (idx > SIGNCMD_LIST) {
       int id = -1;
       CS group = NULL;
       int prio = -1;
       Book* book = NULL;
       LineNr lnum = -1;

       //Parse command line arguments
       CS sign_name = NULL;
       if (parse_sign_cmd_args(idx, arg, OUT &sign_name, &id, &group, &prio, &book, &lnum) == FAIL)
          return;

       if (idx == SIGNCMD_PLACE)
          sign_place_cmd(book, lnum, sign_name, id, group, prio);
       ei (idx == SIGNCMD_UNPLACE)
          sign_unplace_cmd(book, lnum, sign_name, id, group);
       ei (idx == SIGNCMD_JUMP)
          sign_jump_cmd(book, lnum, sign_name, id, group);

       return;
   }

   //Define, undefine or list signs.
   if (idx == SIGNCMD_LIST && *arg == ZERO) {
        //":sign list": list all defined signs
        for (Sign *sp = first_sign; sp && !gotInterruptG; sp = sp->next)
            sign_list_defined(sp);
   } ei (*arg == ZERO) {
        emsg(_(e_missing_sign_name));
   } else {

      //Isolate the sign name.  If it's a number skip leading zeroes,
      //so that "099" and "99" are the same sign.  But keep "0".
      p = skiptowhite(arg);
      if (*p != ZERO)
         *p++ = ZERO;

      while (arg[0] == '0' && arg[1] != ZERO)
         ++arg;

      CS name = copyStr(arg);

      if (idx == SIGNCMD_DEFINE)
         sign_define_cmd(name, p);
      ei (idx == SIGNCMD_LIST)
         //":sign list {name}"
         sign_list_by_name(name);
      else
         //":sign undefine {name}"
         sign_undefine_by_name(name, true);

      eeglFree(name);
   }
}

//Return information about a specified sign
private void
sign_getinfo(Sign* sp, Bag* retBag) {
    bagAddString(retBag, S"name", sp->name);

    if (sp->text)
       bagAddString(retBag, S"text", sp->text);

    if (sp->priority > 0)
       bagAddNumber(retBag, S"priority", sp->priority);

    if (sp->lineHiId > 0) {
       Text p = getHiliteGroupName(NULL, sp->lineHiId);
       if (p.len == 0)
          p = text(S"NONE");
       bagAddString(retBag, S"linehl", p.c);
    }

    if (sp->textHiId > 0) {
        Text p = getHiliteGroupName(NULL, sp->textHiId);
        if (p.len == 0)
           p = text(S"NONE");
        bagAddString(retBag, S"texthl", p.c);
    }

    if (sp->cursorLineHiId > 0) {
        Text p = getHiliteGroupName(NULL, sp->cursorLineHiId);
        if (p.len == 0)
           p = text(S"NONE");
        bagAddString(retBag, S"culhl", p.c);
    }

    if (sp->lineNumHiId > 0) {
        Text p = getHiliteGroupName(NULL, sp->lineNumHiId);
        if (p.len == 0)
           p = text(S"NONE");
        bagAddString(retBag, S"numhl", p.c);
    }
}

//If 'name' is NULL, return a list of all the defined signs.
//Otherwise, return information about the specified sign.
private void
sign_getlist(CS name, List* retlist) {
    Sign* sp = first_sign;

    if (name) {
       sp = sign_find(name, NULL);
       if (!sp)
          return;
    }

    for (; sp && !gotInterruptG; sp = sp->next) {
        Bag *dict = allocBag_id(aid_sign_getlist);
        if (!dict)
           return;

        if (listAppendBag(retlist, dict) == FAIL)
           return;

        sign_getinfo(sp, dict);

        if (name) //handle only the specified sign
           break;
    }
}

//Returns information about signs placed in a book as list of dicts.
pub void
llGetBookSigns(Book *book, List *l){
    SignEntry *sign = NULL;
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
        Bag *d = sign_get_info(sign);
        if (d)
            listAppendBag(l, d);
    }
}

//Return information about all the signs placed in a book
private void
getSignsInBook(
   Book* book,
   LineNr lnum,
   int sign_id,
   CS sign_group,
   List* retlist
) {
   Bag* b = allocBag_id(aid_sign_getplaced_dict);
   if (!b)
      return;

   listAppendBag(retlist, b);

   bagAddNumber(b, S"bufnr", (long)book->fiNum);

   List *l = list_alloc_id(aid_sign_getplaced_list);
   if (!l)
      return;

   bagAddList(b, S"signs", l);

   SignEntry *sign = NULL;
   FOR_ALL_SIGNS_IN_BOOK(book, sign) {
      if (!sign_in_group(sign, sign_group))
          continue;

      if ((lnum == 0 && sign_id == 0) ||
            (sign_id == 0 && lnum == sign->lnum) ||
            (lnum == 0 && sign_id == sign->id) ||
            (lnum == sign->lnum && sign_id == sign->id)
      ) {
         Bag *sdict = sign_get_info(sign);
         if (sdict)
            listAppendBag(l, sdict);
      }
   }
}

//Get a list of signs placed in book. If 'num' is non-zero, return the
//sign placed at the line number. If 'lnum' is zero, return all the signs
//placed in 'book'. If 'book' is NULL, return signs placed in all the books.
private void
sign_get_placed(
   Book* book,
   LineNr lnum,
   int sign_id,
   CS sign_group,
   List* retlist
) {
   if (book) {
      getSignsInBook(book, lnum, sign_id, sign_group, retlist);
   } else {
      FOR_ALL_BOOKS(book) {
         if (book->signList)
            getSignsInBook(book, 0, sign_id, sign_group, retlist);
      }
   }
}

//List one sign.
private void
sign_list_defined(Sign* sp) {
   Byte lbuf[MSG_BUF_LEN];

   smsg("sign %s", sp->name);
   if (sp->text) {
      msg_puts(S" text=");
      msg_outtrans(sp->text);
   }

   if (sp->priority > 0) {
      eeSnprintf(lbuf, MSG_BUF_LEN, " priority=%d", sp->priority);
      msg_puts(lbuf);
   }

   if (sp->lineHiId > 0) {
      msg_puts(S" linehl=");
      Text p = getHiliteGroupName(NULL, sp->lineHiId);
      if (p.len == 0)
         msg_puts(S"NONE");
      else
         msg_puts(p.c);
   }

   if (sp->textHiId > 0) {
       msg_puts(S" texthl=");

       Text p = getHiliteGroupName(NULL, sp->textHiId);
       if (p.len == 0)
          msg_puts(S"NONE");
       else
          msg_puts(p.c);
    }

    if (sp->cursorLineHiId > 0) {
       msg_puts(S" culhl=");

       Text p = getHiliteGroupName(NULL, sp->cursorLineHiId);
       if (p.len == 0)
          msg_puts(S"NONE");
       else
          msg_puts(p.c);
    }

    if (sp->lineNumHiId > 0) {
       msg_puts(S" numhl=");

       Text p = getHiliteGroupName(NULL, sp->lineNumHiId);
       if (p.len == 0)
          msg_puts(S"NONE");
       else
          msg_puts(p.c);
    }
}

//Undefine a sign and free its memory.
private void
sign_undefine(Sign* sp, Sign* sp_prev) {
   eeglFree(sp->name);
   eeglFree(sp->text);

   if (!sp_prev)
      first_sign = sp->next;
   else
      sp_prev->next = sp->next;

   eeglFree(sp);
}

//Undefine/free all signs.
pub void
free_signs(void) {
   while (first_sign)
      sign_undefine(first_sign, NULL);
}

enum {
   EXP_SUBCMD, //expand :sign sub-commands
   EXP_DEFINE, //expand :sign define {name} args
   EXP_PLACE, //expand :sign place {id} args
   EXP_LIST, //expand :sign place args
   EXP_UNPLACE, //expand :sign unplace"
   EXP_SIGN_NAMES, //expand with name of placed signs
   EXP_SIGN_GROUPS //expand with name of placed sign groups
} expandWhatS;

//Return the n'th sign name (used for command line completion)
private CS
get_nth_sign_name(int idx) {
   int current_idx = 0;
   Sign* sp = NULL;

   //Complete with name of signs already defined
   FOR_ALL_SIGNS(sp) {
      if (current_idx++ == idx)
         return sp->name;
   }
   return NULL;
}

//Return the n'th sign group name (used for command line completion)
private CS
get_nth_sign_group_name(int idx) {
   int current_idx = 0;
   int todo = (int)signGroups.count;
   EeSetItem *hi = NULL;

   //Complete with name of sign groups already defined
   FOR_ALL_HASHTAB_ITEMS(&signGroups, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         if (current_idx++ == idx) {
             SignGroup *group = HI2SG(hi);
             return group->sg_name;
         }
      }
   }
   return NULL;
}

//Function given to expandGeneric() to obtain the sign command expansion.
pub CS
get_sign_name(Expand *, int idx) {
    switch (expandWhatS) {
    case EXP_SUBCMD:
       return (CS)cmds[idx];
    case EXP_DEFINE: {
       char *define_arg[] = { 
           "culhl=", "icon=",   "linehl=",   "numhl=", "text=",  "texthl=", "priority=", NULL 
       };
       return (CS)define_arg[idx];
    }
    case EXP_PLACE: {
       char *place_arg[] = { 
          "line=", "name=", "group=", "priority=", "file=", "buffer=", NULL 
       };
       return (CS)place_arg[idx];
    }
    case EXP_LIST: {
       char *list_arg[] = { "group=", "file=", "buffer=", NULL };
       return (CS)list_arg[idx];
    }
    case EXP_UNPLACE: {
       char *unplace_arg[] = { "group=", "file=", "buffer=", NULL };
       return (CS)unplace_arg[idx];
    }
    case EXP_SIGN_NAMES:
       return get_nth_sign_name(idx);
    case EXP_SIGN_GROUPS:
       return get_nth_sign_group_name(idx);
    default:
       return NULL;
    }
}

//Handle command line completion for :sign command.
pub void
set_context_in_sign_cmd(Expand* xp, CS arg) {
   CS p;
   CS last;

   //Default: expand subcommands.
   xp->context = EXPAND_SIGN;
   expandWhatS = EXP_SUBCMD;
   xp->input = mbText(arg);

   CS end_subcmd = skiptowhite(arg);
   //expand subcmd name
   //:sign {subcmd}<CTRL-D>
   if (*end_subcmd == ZERO)
      return;

   int cmd_idx = sign_cmd_idx(arg, end_subcmd);

   //:sign {subcmd} {subcmd_args}
   //               |
   //               begin_subcmd_args
   CS begin_subcmd_args = skipwhite(end_subcmd);

   //expand last argument of subcmd

   //:sign define {name} {args}...
   //             |
   //             p

   //Loop until reaching last argument.
   p = begin_subcmd_args;
   do {
      p = skipwhite(p);
      last = p;
      p = skiptowhite(p);
   } while (*p != ZERO);

   p = firstOccurrence(last, '=');

   //:sign define {name} {args}... {last}=
   //                              |     |
   //                           last     p
   if (p == NULL) {
       //Expand last argument name (before equal sign).
       xp->input = mbText(last);
       switch (cmd_idx) {
       case SIGNCMD_DEFINE:
          expandWhatS = EXP_DEFINE;
          break;
       case SIGNCMD_PLACE:
          //List placed signs
          if (EE_ISDIGIT(*begin_subcmd_args))
             //  :sign place {id} {args}...
             expandWhatS = EXP_PLACE;
          else
             //  :sign place {args}...
             expandWhatS = EXP_LIST;
          break;
       case SIGNCMD_LIST:
       case SIGNCMD_UNDEFINE:
          //:sign list <CTRL-D>
          //:sign undefine <CTRL-D>
          expandWhatS = EXP_SIGN_NAMES;
          break;
       case SIGNCMD_JUMP:
       case SIGNCMD_UNPLACE:
          expandWhatS = EXP_UNPLACE;
          break;
       default:
          xp->context = EXPAND_NOTHING;
       }
   } else {
      //Expand last argument value (after equal sign).
      xp->input = mbText(p + 1);
      switch (cmd_idx) {
      case SIGNCMD_DEFINE:
         if (STRNCMP(last, "texthl", 6) == 0
            || STRNCMP(last, "linehl", 6) == 0
            || STRNCMP(last, "culhl", 5) == 0
            || STRNCMP(last, "numhl", 5) == 0
         )
            xp->context = EXPAND_HILITE_GROUP;
         ei (STRNCMP(last, "icon", 4) == 0)
            xp->context = EXPAND_FILES;
         else
            xp->context = EXPAND_NOTHING;
         break;
      case SIGNCMD_PLACE:
         if (STRNCMP(last, "name", 4) == 0)
            expandWhatS = EXP_SIGN_NAMES;
         ei (STRNCMP(last, "group", 5) == 0)
            expandWhatS = EXP_SIGN_GROUPS;
         ei (STRNCMP(last, "file", 4) == 0)
            xp->context = EXPAND_BUFFERS;
         else
            xp->context = EXPAND_NOTHING;
         break;
      case SIGNCMD_UNPLACE:
      case SIGNCMD_JUMP:
         if (STRNCMP(last, "group", 5) == 0)
            expandWhatS = EXP_SIGN_GROUPS;
         ei (STRNCMP(last, "file", 4) == 0)
            xp->context = EXPAND_BUFFERS;
         else
            xp->context = EXPAND_NOTHING;
         break;
      default:
         xp->context = EXPAND_NOTHING;
      }
   }
}

//Define a sign using the attributes in 'dict'. Returns 0 on success and -1 on failure.
private int
sign_define_from_dict(CS name_arg, Bag* bag) {
   CS linehl = NULL;
   CS text = NULL;
   CS texthl = NULL;
   CS culhl = NULL;
   CS numhl = NULL;
   int prio = -1;
   int retval = -1;

   if (!name_arg && !bag)
      return retval;

   CS name = name_arg ? copyStr(name_arg) : bagGetString(bag, tConst("name"), true);

   if (!name || name[0] == ZERO)
      goto cleanup;

   if (bag) {
      linehl = bagGetString(bag, tConst("linehl"), true);
      text = bagGetString(bag, tConst("text"), true);
      texthl = bagGetString(bag, tConst("texthl"), true);
      culhl = bagGetString(bag, tConst("culhl"), true);
      numhl = bagGetString(bag, tConst("numhl"), true);
      prio = bagGetNumber_def(bag, tConst("priority"), -1);
   }

   if (sign_define_by_name(name, linehl, text, texthl, culhl, numhl, prio) == OK)
      retval = 0;

cleanup:
   eeglFree(name);
   eeglFree(linehl);
   eeglFree(text);
   eeglFree(texthl);
   eeglFree(culhl);
   eeglFree(numhl);

   return retval;
}

//Define multiple signs using attributes from list 'l' and store the return values in 'retlist'.
private void
sign_define_multiple(List* l, List* retlist) {
   ListItem *li = NULL;
   FOR_ALL_LIST_ITEMS(l, li) {
      int retval = -1;

      if (li->c.tag == VAR_BAG)
         retval = sign_define_from_dict(NULL, li->c.bag);
      else
         emsg(_(e_dictionary_required));

      list_append_number(retlist, retval);
   }
}

pub void
f_sign_define(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_LIST && argvars[1].tag == VAR_UNKNOWN) {
      //Define multiple signs
      allocReturnList(returnVar);

      sign_define_multiple(argvars[0].list, returnVar->list);
      return;
   }

   //Define a single sign
   returnVar->number = -1;

   CS name = convertVarToStringSingleUse(&argvars[0]);
   if (!name)
      return;

   if (check_for_oself_arg(argvars, 1) == FAIL)
        return;

   returnVar->number = sign_define_from_dict(
        name, argvars[1].tag == VAR_BAG ? argvars[1].bag : NULL);
}

pub void
f_sign_getdefined(Arr(Var) argvars, Var* returnVar) {
    if (allocReturnList_id(returnVar, aid_sign_getdefined) == FAIL)
       return;

    CS name = NULL;
    if (argvars[0].tag != VAR_UNKNOWN)
        name = tv_get_string(&argvars[0]);

    sign_getlist(name, returnVar->list);
}

pub void
f_sign_getplaced(Arr(Var) argvars, Var* returnVar) {
   Book* book = NULL;
   LineNr lnum = 0;
   int sign_id = 0;
   Byte *group = NULL;

   if (allocReturnList_id(returnVar, aid_sign_getplaced) == FAIL)
       return;

   if (argvars[0].tag != VAR_UNKNOWN) {
      //get signs placed in the specified book
      book = evGetBookArg(&argvars[0]);
      if (!book)
         return;

      if (argvars[1].tag != VAR_UNKNOWN) {
         if (check_for_nonnull_dict_arg(argvars, 1) == FAIL)
            return;

         DictItem *di = NULL;
         Bag *dict = argvars[1].bag;

         if ((di = bagFind(dict, tConst("lnum"))) != NULL) {
            //get signs placed at this line
            Boole notanum = false;
            varGetNumberChk(&di->c, OUT &notanum);
            if (notanum)
               return;

            lnum = tv_get_lnum(&di->c);
         }

         if ((di = bagFind(dict, tConst("id"))) != NULL) {
            //get sign placed with this identifier
            Boole notanum = false;
            sign_id = (int)varGetNumberChk(&di->c, OUT &notanum);
            if (notanum)
               return;
         }

         if ((di = bagFind(dict, tConst("group"))) != NULL) {
            group = convertVarToStringSingleUse(&di->c);
            if (!group)
               return;

            if (*group == '\0') //empty string means global group
               group = NULL;
         }
      }
   }

   sign_get_placed(book, lnum, sign_id, group, returnVar->list);
}

pub void
f_sign_jump(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = -1;

   Boole notanum = false;
   //Sign identifier
   int sign_id = (int)varGetNumberChk(argvars, OUT &notanum);
   if (notanum)
      return;

   if (sign_id <= 0) {
      emsg(_(e_invalid_argument));
      return;
   }

   //Sign group
   CS sign_group = convertVarToStringSingleUse(&argvars[1]);
   if (!sign_group)
       return;

   if (sign_group[0] == '\0') {
      sign_group = NULL; //global sign group
   } else {
      sign_group = copyStr(sign_group);
   }

   //Book to place the sign
   Book* book = evGetBookArg(&argvars[2]);
   if (!book)
       goto cleanup;

   returnVar->number = sign_jump(sign_id, sign_group, book);

cleanup:
   eeglFree(sign_group);
}

//Place a new sign using the values specified in dict 'dict'. Returns the sign
//identifier if successfully placed, otherwise returns 0.
private int
sign_place_from_dict(
   Var* id_tv,
   Var* group_tv,
   Var* name_tv,
   Var* buf_tv,
   Bag* dict
){
   int sign_id = 0;
   CS group = NULL;
   Byte *sign_name = NULL;
   DictItem *di = NULL;
   LineNr lnum = 0;
   int prio = -1;
   int ret_sign_id = -1;

   //sign identifier
   if (!id_tv && (di = bagFind(dict, tConst("id")))) {
      id_tv = &di->c;
   }

   if (!id_tv) {
      sign_id = 0;
   } else {
      Boole notanum = false;
      sign_id = varGetNumberChk(id_tv, OUT &notanum);
      if (notanum)
         return -1;

      if (sign_id < 0) {
         emsg(_(e_invalid_argument));
         return -1;
      }
   }

   //sign group
   if (!group_tv) {
      di = bagFind(dict, tConst("group"));
      if (di)
         group_tv = &di->c;
   }

   if (group_tv == NULL) {
      group = NULL; //global group
   } else {
      group = convertVarToStringSingleUse(group_tv);
      if (!group)
         goto cleanup;

      if (group[0] == '\0') { //global sign group
         group = NULL;
      } else {
         group = copyStr(group);
      }
   }

   //sign name
   if (!name_tv) {
      di = bagFind(dict, tConst("name"));
      if (di)
         name_tv = &di->c;
   }

   if (name_tv == NULL)
      goto cleanup;

   sign_name = convertVarToStringSingleUse(name_tv);
   if (sign_name == NULL)
      goto cleanup;

   //buffer to place the sign
   if (buf_tv == NULL) {
        di = bagFind(dict, tConst("buffer"));
        if (di)
           buf_tv = &di->c;
   }

   if (!buf_tv)
      goto cleanup;

   Book* book = evGetBookArg(buf_tv);
   if (!book)
      goto cleanup;

   //line number of the sign
   di = bagFind(dict, tConst("lnum"));
   if (di) {
      lnum = tv_get_lnum(&di->c);
      if (lnum <= 0) {
            emsg(_(e_invalid_argument));
            goto cleanup;
        }
    }

    //sign priority
    di = bagFind(dict, tConst("priority"));
    if (di) {
        Boole notanum = false;
        prio = (int)varGetNumberChk(&di->c, OUT &notanum);
        if (notanum)
           goto cleanup;
    }

    if (sign_place(&sign_id, group, sign_name, book, lnum, prio) == OK)
        ret_sign_id = sign_id;

cleanup:
    eeglFree(group);

    return ret_sign_id;
}

pub void
f_sign_place(Arr(Var) argvars, Var* returnVar) {
   Bag* bag = NULL;
   returnVar->number = -1;

   if (argvars[4].tag != VAR_UNKNOWN) {
        if (check_for_nonnull_dict_arg(argvars, 4) == FAIL)
            return;
        bag = argvars[4].bag;
   }

   returnVar->number = sign_place_from_dict(&argvars[0], &argvars[1], &argvars[2], &argvars[3], bag);
}

//"sign_placelist()" function.  Place multiple signs.
pub void
f_sign_placelist(Arr(Var) argvars, Var* returnVar) {
   allocReturnList(returnVar);
   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   //Process the List of sign attributes
   ListItem *li = NULL;
   FOR_ALL_LIST_ITEMS(argvars[0].list, li) {
      int sign_id = -1;

      if (li->c.tag == VAR_BAG)
         sign_id = sign_place_from_dict(NULL, NULL, NULL, NULL, li->c.bag);
      else
         emsg(_(e_dictionary_required));

      list_append_number(returnVar->list, sign_id);
   }
}

//Undefine multiple signs
private void
sign_undefine_multiple(List *l, List *retlist) {
    ListItem *li = NULL;
    FOR_ALL_LIST_ITEMS(l, li)
    {
        int retval = -1;
        Byte *name = convertVarToStringSingleUse(&li->c);
        if (name != NULL && (sign_undefine_by_name(name, true) == OK))
            retval = 0;

        list_append_number(retlist, retval);
    }
}

pub void
f_sign_undefine(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_LIST && argvars[1].tag == VAR_UNKNOWN) {
        //Undefine multiple signs
        allocReturnList(returnVar);

        sign_undefine_multiple(argvars[0].list, returnVar->list);
        return;
   }

   returnVar->number = -1;
   if (argvars[0].tag == VAR_UNKNOWN) {
      //Free all the signs
      free_signs();
      returnVar->number = 0;
   } else {
       //Free only the specified sign
       Byte *name = convertVarToStringSingleUse(&argvars[0]);
       if (name == NULL)
          return;

       if (sign_undefine_by_name(name, true) == OK)
           returnVar->number = 0;
   }
}

//Unplace the sign with attributes specified in 'dict'. Returns 0 on success and -1 on failure.
private int
sign_unplace_from_dict(Var *group_tv, Bag *dict) {
   int sign_id = 0;
   Book* book = NULL;
   int retval = -1;

   //sign group
   CS group = group_tv ? tv_get_string(group_tv) : bagGetString(dict, tConst("group"), false);

   if (group) {
      if (group[0] == '\0') { //global sign group
         group = NULL;
      } else {
         group = copyStr(group);
      }
   }

    if (dict) {
      DictItem *di = bagFind(dict, tConst("buffer"));
      if (di) {
         book = evGetBookArg(&di->c);
          if (!book)
              goto cleanup;
      }

      if (bagHasKey(dict, tConst("id"))) {
         sign_id = bagGetNumber(dict, tConst("id"));
         if (sign_id <= 0) {
            emsg(_(e_invalid_argument));
            goto cleanup;
         }
      }
   }

   if (!book) {
      //Delete the sign in all the books
      retval = 0;
      FOR_ALL_BOOKS(book) {
         if (sign_unplace(sign_id, group, book, 0) != OK)
            retval = -1;
      } 
   } ei (sign_unplace(sign_id, group, book, 0) == OK)
      retval = 0;

cleanup:
   eeglFree(group);

   return retval;
}

private NULLABLE SignEntry *
get_first_valid_sign(Portal *wp) {
   SignEntry* sign = wp->book->signList;
   while (sign && !signIsVisible(sign, wp))
      sign = sign->next;
   return sign;
}

pub Boole
isSigncolumnOn(Portal* po) {
   return get_first_valid_sign(po) != NULL ? po->o.signColumn : false;
}

pub void
f_sign_unplace(Arr(Var) argvars, Var* returnVar) {
   Bag *dict = NULL;
   returnVar->number = -1;

   if ((check_for_string_arg(argvars, 0) == FAIL || check_for_oself_arg(argvars, 1) == FAIL))
      return;

   if (argvars[1].tag != VAR_UNKNOWN)
      dict = argvars[1].bag;

   returnVar->number = sign_unplace_from_dict(&argvars[0], dict);
}

pub void
f_sign_unplacelist(Arr(Var) argvars, Var* returnVar) {
   allocReturnList(returnVar);

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   ListItem *li = NULL;
   FOR_ALL_LIST_ITEMS(argvars[0].list, li) {
      int retval = -1;

      if (li->c.tag == VAR_BAG)
         retval = sign_unplace_from_dict(NULL, li->c.bag);
      else
         emsg(_(e_dictionary_required));

      list_append_number(returnVar->list, retval);
   }
}

//}}}
//{{{searches


//Values for flags argument for findmatchlimit()
pub
#define FM_BACKWARD  0x01   //search backwards
#define FM_FORWARD   0x02   //search forwards
#define FM_BLOCKSTOP 0x04   //stop at start/end of block
#define FM_SKIPCOMM  0x08   //skip comments
#define SEARCH_STAT_DEF_TIMEOUT 40L

//'W ':  2 +
//'[>9999/>9999]': 13 + 1 (ZERO)
#define SEARCH_STAT_BUF_LEN 16

//This file contains various searching-related routines. These fall into 3 groups:
//0. string searches (for /, ?, n, and N)
//1. character searches within a single line (for f, F, t, T, etc)
//2. "other" kinds of searches like the '%' command, and 'word' searches.

//String searches
//
//The string search functions are divided into two levels:
//lowest:  searchit(); uses an Pos for starting position and found match.
//Highest: do_search(); uses curPor->cursor; calls searchit().
//
//The last search pattern is remembered for repeating the same search.
//This pattern is shared between the :g, :s, ? and / commands.
//This is in search_regcomp().
//
//The actual string matching is done using a heavily modified version of
//Henry Spencer's regular expression library.  See regexp.c.

//Two search patterns are remembered: One for the :substitute command and
//one for other searches.  last_idx points to the one that was used the last time.
private SearchPattern prevSearchPatternsP[2] = {
    {(Text){NULL, 0}, true, false, {'/', 0, 0, 0L}},   //last used search pat
    {(Text){NULL, 0}, true, false, {'/', 0, 0, 0L}}   //last used substitute pat
};

//copy of prevSearchPatternsP[], for keeping the search patterns while executing autocmds
private SearchPattern saved_spats[2];

private int last_idx = 0;   //index in prevSearchPatternsP[] for RE_LAST

private Byte lastc[2] = {ZERO, ZERO};   //last character searched for
private int lastcdir = FORWARD;      //last direction of character search
private int last_t_cmd = true;      //last search t_cmd
private Byte lastc_bytes[MB_MAXBYTES + 1];
private int lastc_bytelen = 1;   //>1 for multi-byte char

private Text mrPatternSaved = (Text){NULL, 0};
private int saved_spats_last_idx = 0;
private Boole saved_spatsHlsearch = true;

//allocated copy of pattern used by search_regcomp()
private Text mrPatternP = (Text){.c = null, .len = 0};

//Type used by find_pattern_in_path() to remember which included files have been searched already
typedef struct {
   FILE* fp;     //File pointer
   CS name;      //Full name of file
   LineNr lnum;  //Line we were up to in file
   int matched;  //Found a match in this file
} SearchedFile;

//translate search pattern for compileRegexp()
//pat_save == RE_SEARCH: save pat in prevSearchPatternsP[RE_SEARCH].pat (normal search cmd)
//pat_save == RE_SUBST: save pat in prevSearchPatternsP[RE_SUBST].pat (:substitute command)
//pat_save == RE_BOTH: save pat in both patterns (:global command)
//pat_use  == RE_SEARCH: use previous search pattern if "pat" is NULL
//pat_use  == RE_SUBST: use previous substitute pattern if "pat" is NULL
//pat_use  == RE_LAST: use last used pattern if "pat" is NULL
//options & SEARCH_HIS: put search string in history
//options & SEARCH_KEEP: keep previous search pattern
//return FAIL if failed, OK otherwise.
pub int
search_regcomp(
   Text pat,
   Arr(CS) used_pat,
   int pat_save,
   int pat_use,
   int options,
   OUT RegMultilineMatch* regmatch   //return: pattern and ignore-case flag
){
   int magic;
   anyRegexEmsgG = false;

   //If no pattern given, use a previously defined pattern.
   if (pat.len == 0) {
      int i = (pat_use == RE_LAST) ? last_idx : pat_use;
      if (prevSearchPatternsP[i].pat.c == 0) {   //pattern was never defined
         if (pat_use == RE_SUBST)
            emsg(_(e_no_previous_substitute_regular_expression));
         else
            emsg(_(e_no_previous_regular_expression));
         anyRegexEmsgG = true;
         return FAIL;
      }
      pat = prevSearchPatternsP[i].pat;
      magic = prevSearchPatternsP[i].magic;
      no_smartcase = prevSearchPatternsP[i].no_scs;
   } ei (options & SEARCH_HIS)   //put new pattern in history
      scrAddToHistory(HIST_SEARCH, pat, true, ZERO);

   if (used_pat)
      *used_pat = pat.c;

   eeglFree(mrPatternP.c);
   mrPatternP = copyText(pat);

   //Save the currently used pattern in the appropriate place,
   //unless the pattern should not be remembered.
   if (!(options & SEARCH_KEEP) && (commModifierG.cmod_flags & CMOD_KEEPPATTERNS) == 0) {
      //search or global command
      if (pat_save == RE_SEARCH || pat_save == RE_BOTH)
          save_re_pat(RE_SEARCH, pat, magic);
      //substitute or global command
      if (pat_save == RE_SUBST || pat_save == RE_BOTH)
          save_re_pat(RE_SUBST, pat, magic);
   }

   regmatch->rmm_ic = ignorecase(pat.c);
   regmatch->rmm_maxcol = 0;
   regmatch->regprog = compileRegexp(pat.c, magic ? RE_MAGIC : 0);
   if (regmatch->regprog == NULL)
      return FAIL;
   return OK;
}

//Get search pattern used by search_regcomp().
pub CS
get_search_pat(void) {
   return mrPatternP.c;
}

pub void
save_re_pat(int idx, Text pat, int magic) {
   if (prevSearchPatternsP[idx].pat.c == pat.c)
      return;

   eeglFree(prevSearchPatternsP[idx].pat.c);
   prevSearchPatternsP[idx].pat = copyText(pat);
   prevSearchPatternsP[idx].magic = magic;
   prevSearchPatternsP[idx].no_scs = no_smartcase;
   last_idx = idx;
   //If @hlsearch set and search pat changed: need redraw.
   if ((p_hls && idx == RE_SEARCH) != 0)
      redraw_all_later(UPD_SOME_VALID);
   setHlsearch(true);
}

//Save the search patterns, so they can be restored later.
//Used before/after executing autocommands and user functions.
private int saveLevelS = 0;

pub void
save_search_patterns(void) {
   if (saveLevelS++ != 0)
      return;

   for (int i = 0; i < (int)ARRAY_LENGTH(prevSearchPatternsP); ++i) {
      saved_spats[i] = prevSearchPatternsP[i];
      if (prevSearchPatternsP[i].pat.len != 0) {
         saved_spats[i].pat = copyText(prevSearchPatternsP[i].pat);
      }
   }
   if (mrPatternP.len == 0)
      mrPatternSaved = (Text){NULL, 0};
   else
      mrPatternSaved = copyText(mrPatternP);
   saved_spats_last_idx = last_idx;
   saved_spatsHlsearch = hiliteSearchG;
}

pub void
restore_search_patterns(void) {
   if (--saveLevelS != 0)
      return;

   for (Unt i = 0; i < ARRAY_LENGTH(prevSearchPatternsP); ++i) {
      eeglFree(prevSearchPatternsP[i].pat.c);
      prevSearchPatternsP[i] = saved_spats[i];
   }
   eeglFree(mrPatternP.c);
   mrPatternP = mrPatternSaved;
   last_idx = saved_spats_last_idx;
   setHlsearch(saved_spatsHlsearch);
}

#if defined(EXITFREE)
pub void
free_search_patterns(void) {
   for (int i = 0; i < (int)ARRAY_LENGTH(prevSearchPatternsP); ++i) {
      EE_CLEAR(prevSearchPatternsP[i].pat);
      prevSearchPatternsP[i].patlen = 0;
   }
   EE_CLEAR(mrPatternP.c);
   mrPatternLen = 0;
}
#endif

//copy of prevSearchPatternsP[RE_SEARCH], for keeping the search patterns while incremental
//searching
private SearchPattern saved_last_search_spat;
private int did_save_last_search_spat = 0;
private int saved_last_idx = 0;
private Boole savedHlsearch = true;
private int saved_search_match_endcol;
private int saved_search_match_lines;

//Save and restore the search pattern for incremental highlight search feature.
//
//It's similar to but different from save_search_patterns() and
//restore_search_patterns(), because the search pattern must be restored when
//canceling incremental searching even if it's called inside user functions.
pub void
save_last_search_pattern(void) {
   if (++did_save_last_search_spat != 1)
      //nested call, nothing to do
      return;

   saved_last_search_spat = prevSearchPatternsP[RE_SEARCH];
   if (prevSearchPatternsP[RE_SEARCH].pat.len != 0) {
      saved_last_search_spat.pat = copyText(prevSearchPatternsP[RE_SEARCH].pat);
   }
   saved_last_idx = last_idx;
   savedHlsearch = hiliteSearchG;
}

pub void
restore_last_search_pattern(void) {
   if (--did_save_last_search_spat > 0)
      //nested call, nothing to do
      return;
   if (did_save_last_search_spat != 0) {
      internalErrMsg(S"restore_last_search_pattern() called more often than save_last_search_pattern()");
      return;
   }

   eeglFree(prevSearchPatternsP[RE_SEARCH].pat.c);
   prevSearchPatternsP[RE_SEARCH] = saved_last_search_spat;
   saved_last_search_spat.pat = (Text){NULL, 0};
   last_idx = saved_last_idx;
   setHlsearch(savedHlsearch);
}

//Save and restore the incsearch hiliting variables.
//This is required so that calling searchcount() at does not invalidate the incsearch hiliting.
private void
save_incsearch_state(void) {
   saved_search_match_endcol = search_match_endcol;
   saved_search_match_lines  = search_match_lines;
}

private void
restore_incsearch_state(void) {
   search_match_endcol = saved_search_match_endcol;
   search_match_lines  = saved_search_match_lines;
}

pub Text
last_search_pattern(void) {
   return prevSearchPatternsP[RE_SEARCH].pat;
}

//Return true when case should be ignored for search pattern "pat".
//Use the 'ignorecase' and 'smartcase' options.
pub int
ignorecase(CS pat) {
   return ignorecase_opt(pat, p_ic, p_scs);
}

//As ignorecase() but pass the "ic" and "scs" flags.
pub int
ignorecase_opt(CS pat, int ic_in, int scs) {
   int      ic = ic_in;

   if (ic && !no_smartcase && scs && !(ctrl_x_mode_not_default() && curBook->o.inferCase))
      ic = !pat_has_uppercase(pat);
   no_smartcase = false;
   return ic;
}

//Return true if pattern "pat" has an uppercase character.
pub int
pat_has_uppercase(CS pat) {
   CS p = pat;
   Magic magic_val = MAGIC_ON;

   //get the magicness of the pattern
   (void)skip_regexp_ex(pat, ZERO, true, NULL, NULL, &magic_val);

   while (*p != ZERO) {
      int      l;

      if ((l = utfCharLen(p)) > 1) {
         if (utf_isupper(mb_ptr2char(p)))
            return true;
         p += l;
      } ei (*p == '\\' && magic_val <= MAGIC_ON) {
         if (p[1] == '_' && p[2] != ZERO)  //skip "\_X"
            p += 3;
         ei (p[1] == '%' && p[2] != ZERO)  //skip "\%X"
            p += 3;
         ei (p[1] != ZERO)  //skip "\X"
            p += 2;
         else
            p += 1;
      } ei ((*p == '%' || *p == '_') && magic_val == MAGIC_ALL) {
         if (p[1] != ZERO)  //skip "_X" and %X
            p += 2;
         else
            p++;
      } ei (MB_ISUPPER(*p))
         return true;
      else
         ++p;
   }
   return false;
}

pub CS
last_csearch(void) {
   return lastc_bytes;
}

pub int
last_csearch_forward(void) {
   return lastcdir == FORWARD;
}

pub int
last_csearch_until(void) {
   return last_t_cmd == true;
}

pub void
set_last_csearch(int c, CS s, int len) {
   *lastc = c;
   lastc_bytelen = len;
   if (len)
      memcpy(lastc_bytes, s, len);
   else
      CLEAR_FIELD(lastc_bytes);
}

pub void
set_csearch_direction(int cdir) {
   lastcdir = cdir;
}

pub void
set_csearch_until(int t_cmd) {
   last_t_cmd = t_cmd;
}

pub Text
last_search_pat(void) {
   return prevSearchPatternsP[last_idx].pat;
}

//Reset search direction to forward.  For "gd" and "gD" commands.
pub void
reset_search_dir(void) {
   prevSearchPatternsP[0].off.dir = '/';
}

//Set the last search pattern.  For ":let @/ =" and eeglinfo.
//Also set the saved search pattern, so that this works in an autocommand.
pub void
set_last_search_pat(
   CS s,
   int idx,
   int magic,
   int setlast
) {
   eeglFree(prevSearchPatternsP[idx].pat.c);
   //An empty string means that nothing should be matched.
   if (*s == ZERO)
      prevSearchPatternsP[idx].pat.len = 0;
   else {
      prevSearchPatternsP[idx].pat = 
         (Text){copySubstr(s, prevSearchPatternsP[idx].pat.len), STRLEN(s)};
   }
   prevSearchPatternsP[idx].magic = magic;
   prevSearchPatternsP[idx].no_scs = false;
   prevSearchPatternsP[idx].off.dir = '/';
   prevSearchPatternsP[idx].off.line = false;
   prevSearchPatternsP[idx].off.end = false;
   prevSearchPatternsP[idx].off.off = 0;
   if (setlast)
      last_idx = idx;
   if (saveLevelS) {
      eeglFree(saved_spats[idx].pat.c);
      saved_spats[idx] = prevSearchPatternsP[0];
      if (prevSearchPatternsP[idx].pat.len == 0)
         saved_spats[idx].pat.len = 0;
      else
         saved_spats[idx].pat = copyText(prevSearchPatternsP[idx].pat);
      saved_spats_last_idx = last_idx;
   }
   //If @hlsearch set and search pat changed: need redraw.
   if (p_hls && idx == RE_SEARCH && hiliteSearchG)
      redraw_all_later(UPD_SOME_VALID);
}

//Get a regexp program for the last used search pattern. This is used for hiliting all matches 
//in a portal. Values returned in regmatch->regprog and regmatch->rmm_ic.
pub void
last_pat_prog(RegMultilineMatch* regmatch) {
   if (prevSearchPatternsP[RE_SEARCH].pat.len == 0) {
      regmatch->regprog = NULL;
      return;
   }
   ++emsg_off;      //So it doesn't beep if bad expr
   (void)search_regcomp((Text){null, 0}, NULL, 0, RE_SEARCH, SEARCH_KEEP, OUT regmatch);
   --emsg_off;
}

//Lowest level search function.
//Search for 'count'th occurrence of pattern "pat" in direction "dir".
//Start at position "pos" and return the found position in "pos".
//
//if (options & SEARCH_MSG) == 0 don't give any messages
//if (options & SEARCH_MSG) == SEARCH_NFMSG don't give 'notfound' messages
//if (options & SEARCH_MSG) == SEARCH_MSG give all messages
//if (options & SEARCH_HIS) put search pattern in history
//if (options & SEARCH_END) return position at end of match
//if (options & SEARCH_START) accept match at pos itself
//if (options & SEARCH_KEEP) keep previous search pattern
//if (options & SEARCH_FOLD) match only once in a closed fold
//if (options & SEARCH_PEEK) check for typed char, cancel search
//if (options & SEARCH_COL) start at pos->col instead of zero
//
//Return FAIL (zero) for failure, non-zero for success.
//Return the index of the first matching subpattern plus one; one if there was none.
pub int
searchit(
   Portal* port, //portal to search in; can be NULL for a buffer without a portal!
   Book* book,
   Pos* pos,
   OUT Pos* end_pos,   //set to end of the match, unless NULL
   Unt dir,    //forward or backward
   Text pat,
   long count,
   Unt options,
   int pat_use,   //which pattern to use when "pat" is empty
   SearchitArg* extra_arg   //optional extra arguments, can be NULL
){
   int      found;
   LineNr   lnum;      //no init to shut up Apollo cc
   ColNr   col;
   RegMultilineMatch   regmatch;
   CS ptr;
   ColNr   matchcol;
   PosNoVirt   endpos;
   PosNoVirt   matchpos;
   int loop;
   Pos   start_pos;
   int at_first_line;
   int extra_col;
   int start_char_len;
   int match_ok;
   long nmatched;
   int submatch = 0;
   int first_match = true;
   int called_emsg_before = called_emsg;
   int break_loop = false;
   LineNr   stop_lnum = 0;   //stop after this line number when != 0
   int      unused_timeout_flag = false;
   int      *timed_out = &unused_timeout_flag;  //set when timed out.

   if (search_regcomp(pat, NULL, RE_SEARCH, pat_use,
         (options & (SEARCH_HIS + SEARCH_KEEP)), OUT &regmatch) == FAIL
   ){
      if ((options & SEARCH_MSG) && !anyRegexEmsgG)
         showErrFmtMsg(_(e_invalid_search_string_str), mrPatternP.c);
      return FAIL;
   }

   if (extra_arg) {
      stop_lnum = extra_arg->sa_stop_lnum;
      if (extra_arg->sa_tm > 0)
         init_regexp_timeout(extra_arg->sa_tm);
      //Also set the pointer when sa_tm is zero, the caller may have set the
      //timeout.
      timed_out = &extra_arg->sa_timed_out;
   }

   //find the string
   do {  //loop for count
         //When not accepting a match at the start position set "extra_col" to
         //a non-zero value.  Don't do that when starting at MAXCOL, since MAXCOL + 1 is zero.
         if (pos->col == MAXCOL)
             start_char_len = 0;
         //Watch out for the "col" being MAXCOL - 2, used in a closed fold.
         ei (pos->lnum >= 1 && pos->lnum <= book->mem.lineCount && pos->col < MAXCOL - 2){
            ptr = memGetLine(book, pos->lnum, false);
            if (memGetBookLen(book, pos->lnum) <= pos->col)
               start_char_len = 1;
            else
               start_char_len = utfCharLen(ptr + pos->col);
         } else
             start_char_len = 1;
         if (dir == FORWARD) {
            if (options & SEARCH_START)
               extra_col = 0;
            else
               extra_col = start_char_len;
         } else {
            if (options & SEARCH_START)
               extra_col = start_char_len;
            else
               extra_col = 0;
      }

      start_pos = *pos;   //remember start pos for detecting no match
      found = 0;      //default: not found
      at_first_line = true;   //default: start in first line
      if (pos->lnum == 0) {   //correct lnum for when starting in line 0
         pos->lnum = 1;
         pos->col = 0;
         at_first_line = false;  //not in first line now
      }

      //Start searching in current line, unless searching backwards and we're in column 0.
      //If we are searching backwards, in column 0, and not including the
      //current position, gain some efficiency by skipping back a line.
      //Otherwise begin the search in the current line.
      if (dir == BACKWARD && start_pos.col == 0 && (options & SEARCH_START) == 0) {
         lnum = pos->lnum - 1;
         at_first_line = false;
      } else
         lnum = pos->lnum;

      for (loop = 0; loop <= 1; ++loop) {  //loop twice if 'wrapscan' set
         for ( ; lnum > 0 && lnum <= book->mem.lineCount; lnum += dir, at_first_line = false) {
            //Stop after checking "stop_lnum", if it's set.
            if (stop_lnum != 0 && (dir == FORWARD ? lnum > stop_lnum : lnum < stop_lnum))
               break;
            //Stop after passing the time limit.
            if (*timed_out)
               break;

            //Look for a match somewhere in line "lnum".
            col = at_first_line && (options & SEARCH_COL) ? pos->col : (ColNr)0;
            nmatched = eeRegexec_multi(&regmatch, port, book, lnum, col, timed_out);
            //eeRegexec_multi() may clear "regprog"
            if (regmatch.regprog == NULL)
               break;
            //Abort searching on an error (e.g., out of stack).
            if (called_emsg > called_emsg_before || *timed_out)
                break;
            if (nmatched > 0) {
               //match may actually be in another line when using \zs
               matchpos = regmatch.startpos[0];
               endpos = regmatch.endpos[0];
               submatch = first_submatch(&regmatch);
               //"lnum" may be past end of buffer for "\n\zs".
               if (lnum + matchpos.lnum > book->mem.lineCount)
                  ptr = (CS)"";
               else
                  ptr = memGetLine(book, lnum + matchpos.lnum, false);

               //Forward search in the first line: match should be after the start position. If 
               //not, continue at the end of the match (this is vi compatible) or on the next char.
               if (dir == FORWARD && at_first_line) {
               match_ok = true;

               //When the match starts in a next line it's certainly past the start position.
               //When match lands on a ZERO the cursor will be put
               //one back afterwards, compare with that position,
               //otherwise "/$" will get stuck on end of line.
               while (matchpos.lnum == 0
                  && ((options & SEARCH_END) && first_match
                      ?  (nmatched == 1
                     && (int)endpos.col - 1
                          < (int)start_pos.col + extra_col)
                      : ((int)matchpos.col
                          - (ptr[matchpos.col] == ZERO)
                         < (int)start_pos.col + extra_col)))
               {
                  //otherwise continue one position forward.
                  if (nmatched > 1) {
                      //end is in next line, thus no match in this line
                      match_ok = false;
                      break;
                  }
                  matchcol = endpos.col;
                  //for empty match: advance one char
                  if (matchcol == matchpos.col && ptr[matchcol] != ZERO) {
                      matchcol += utfCharLen(ptr + matchcol);
                  }
                  if (matchcol == 0 && (options & SEARCH_START))
                     break;
                  if (ptr[matchcol] == ZERO
                       || (nmatched = eeRegexec_multi(&regmatch,
                            port, book, lnum + matchpos.lnum,
                            matchcol, timed_out)) == 0
                  ) {
                     match_ok = false;
                     break;
                  }
                  //eeRegexec_multi() may clear "regprog"
                  if (regmatch.regprog == NULL)
                     break;
                  matchpos = regmatch.startpos[0];
                  endpos = regmatch.endpos[0];
                  submatch = first_submatch(&regmatch);

                  //Need to get the line pointer again, a multi-line search may have invalidated it
                  ptr = memGetLine(book, lnum + matchpos.lnum, false);
               }
               if (!match_ok)
                  continue;
               }
               if (dir == BACKWARD) {
                  //Now, if there are multiple matches on this line, we have to get the last one. 
                  //Or the last one before the cursor, if we're on that line.
                  //When putting the new cursor at the end, compare relative to the end of the match
                  match_ok = false;
                  for (;;) {
                      //Remember a position that is before the start
                      //position, we use it if it's the last match in
                      //the line.  Always accept a position after wrapping around.
                      if (loop
                           || ((options & SEARCH_END)
                               ? (lnum + regmatch.endpos[0].lnum < start_pos.lnum
                                    || (lnum + regmatch.endpos[0].lnum == start_pos.lnum
                                         && (int)regmatch.endpos[0].col - 1
                                             < (int)start_pos.col + extra_col))
                               : (lnum + regmatch.startpos[0].lnum < start_pos.lnum
                                    || (lnum + regmatch.startpos[0].lnum == start_pos.lnum
                                         && (int)regmatch.startpos[0].col 
                                            < (int)start_pos.col + extra_col)
                                 )
                           )
                     ) {
                        match_ok = true;
                        matchpos = regmatch.startpos[0];
                        endpos = regmatch.endpos[0];
                        submatch = first_submatch(&regmatch);
                     } else
                        break;

                     //We found a valid match, now check if there is
                     //another one after it. continue one position forward.
                     if (nmatched > 1)
                        break;
                     matchcol = endpos.col;
                     //for empty match: advance one char
                     if (matchcol == matchpos.col && ptr[matchcol] != ZERO) {
                        matchcol += utfCharLen(ptr + matchcol);
                     }
                     if (ptr[matchcol] == ZERO
                         || (nmatched = eeRegexec_multi(&regmatch,
                              port, book, lnum + matchpos.lnum,
                              matchcol, timed_out)) == 0
                     ) {
                        //If the search timed out, we did find a match
                        //but it might be the wrong one, so that's not OK.
                        if (*timed_out)
                           match_ok = false;
                        break;
                     }
                     //eeRegexec_multi() may clear "regprog"
                     if (regmatch.regprog == NULL)
                        break;

                      //Need to get the line pointer again, a multi-line search may have 
                      //invalidated it
                      ptr = memGetLine(book, lnum + matchpos.lnum, false);
                  }

                  //If there is only a match after the cursor, skip this match.
                  if (!match_ok)
                     continue;
                }

               //With the SEARCH_END option move to the last character of the match. Don't do it 
               //for an empty match, end should be same as start then.
               if ((options & SEARCH_END) && !(options & SEARCH_NOOF)
                   && !(matchpos.lnum == endpos.lnum && matchpos.col == endpos.col)
               ) {
                  //For a match in the first column, set the position
                  //on the ZERO in the previous line.
                  pos->lnum = lnum + endpos.lnum;
                  pos->col = endpos.col;
                  if (endpos.col == 0) {
                     if (pos->lnum > 1) { //just in case
                        --pos->lnum;
                        pos->col = memGetBookLen(book, pos->lnum);
                     }
                  } else {
                      --pos->col;
                      if (pos->lnum <= book->mem.lineCount) {
                        ptr = memGetLine(book, pos->lnum, false);
                        pos->col -= (*mb_head_off)(ptr, ptr + pos->col);
                      }
                  }
                  if (end_pos) {
                      end_pos->lnum = lnum + matchpos.lnum;
                      end_pos->col = matchpos.col;
                  }
               } else {
                  pos->lnum = lnum + matchpos.lnum;
                  pos->col = matchpos.col;
                  if (end_pos) {
                      end_pos->lnum = lnum + endpos.lnum;
                      end_pos->col = endpos.col;
                  }
               }
               pos->coladd = 0;
               if (end_pos)
                  end_pos->coladd = 0;
               found = 1;
               first_match = false;

                //Set variables used for 'incsearch' hiliting.
                search_match_lines = endpos.lnum - matchpos.lnum;
                search_match_endcol = endpos.col;
                break;
            }
            line_breakcheck();   //stop if ctrl-C typed
            if (gotInterruptG)
                break;

            //Cancel searching if a character was typed.  Used for
            //'incsearch'.  Don't check too often, that would slowdown searching too much.
            if ((options & SEARCH_PEEK) && ((lnum - pos->lnum) & 0x3f) == 0 && char_avail()) {
                break_loop = true;
                break;
            }

            if (loop && lnum == start_pos.lnum)
                break;       //if second loop, stop where started
         }
         at_first_line = false;

         //eeRegexec_multi() may clear "regprog"
         if (regmatch.regprog == NULL)
            break;

         //Stop the search if wrapscan isn't set, "stop_lnum" is
         //specified, after an interrupt, after a match and after looping twice.
         if (wrapSearchG || stop_lnum != 0 || gotInterruptG
                  || called_emsg > called_emsg_before || *timed_out
                  || break_loop
                  || found || loop)
            break;

         //If 'wrapscan' is set we continue at the other end of the file.
         //If 'shortmess' does not contain 's', we give a message, but
         //only, if we won't show the search stat later anyhow,
         //(so SEARCH_COUNT must be absent).
         //This message is also remembered in msgAfterRedrawG for when the screen is redrawn. 
         //The msgAfterRedrawG is cleared whenever another message is written.
         if (dir == BACKWARD)    //start second loop at the other end
            lnum = book->mem.lineCount;
         else
            lnum = 1;
         if (extra_arg != NULL)
            extra_arg->sa_wrapped = true;
      }
      if (gotInterruptG || called_emsg > called_emsg_before || *timed_out || break_loop)
         break;
   } while (--count > 0 && found);   //stop after count matches or no match

   if (extra_arg && extra_arg->sa_tm > 0)
      disable_regexp_timeout();
   eeRegFree(regmatch.regprog);

   if (!found) {         //did not find it
      if (gotInterruptG)
         emsg(_(e_interrupted));
      ei ((options & SEARCH_MSG) == SEARCH_MSG) {
         if (wrapSearchG)
            showErrFmtMsg(_(e_pattern_not_found_str), mrPatternP.c);
      }
      return FAIL;
   }

   //A pattern like "\n\zs" may go past the last line.
   if (pos->lnum > book->mem.lineCount) {
      pos->lnum = book->mem.lineCount;
      pos->col = memGetBookLen(book, pos->lnum);
      if (pos->col > 0)
         --pos->col;
   }

   return submatch + 1;
}

pub void
set_search_direction(int cdir) {
   prevSearchPatternsP[0].off.dir = cdir;
}

//Return the number of the first subpat that matched. Return zero if none of them matched.
private int
first_submatch(RegMultilineMatch *rp) {
   int      submatch;

   for (submatch = 1; ; ++submatch) {
      if (rp->startpos[submatch].lnum >= 0)
          break;
      if (submatch == 9) {
          submatch = 0;
          break;
      }
   }
   return submatch;
}

//Highest level string search function.
//Search for the 'count'th occurrence of pattern 'pat' in direction 'dirc'
//      If 'dirc' is 0: use previous dir.
//  If 'pat' is NULL or empty : use previous string.
//  If 'options & SEARCH_REV' : go in reverse of previous dir.
//  If 'options & SEARCH_ECHO': echo the search command and handle options
//  If 'options & SEARCH_MSG' : may give error message
//  If 'options & SEARCH_OPT' : interpret optional flags
//  If 'options & SEARCH_HIS' : put search pattern in history
//  If 'options & SEARCH_NOOF': don't add offset to position
//  If 'options & SEARCH_MARK': set previous context mark
//  If 'options & SEARCH_KEEP': keep previous search pattern
//  If 'options & SEARCH_START': accept match at curpos itself
//  If 'options & SEARCH_PEEK': check for typed char, cancel search
//
//Careful: If prevSearchPatternsP[0].off.line == true and prevSearchPatternsP[0].off.off == 0 this
//makes the movement linewise without moving the match position.
//
//Return 0 for failure, 1 for found, 2 for found and line offset added.
pub int
do_search(
   Operator* oap,   //can be NULL
   int dirc,   //'/' or '?'
   int search_delim, //the delimiter for the search, e.g. '%' in s%regex%replacement%
   Text pat,
   long count,
   int options,
   SearchitArg* sia   //optional arguments or NULL
){
   Text searchstr;
   SearchOffset       old_off;
   int retval;   //Return value
   CS p;
   long c;
   CS dircp;
   CS strcopy = NULL;
   CS ps;
   Boole showSearchStats;
   CS msgbuf = NULL;
   Unt msgbuflen = 0;
   int has_offset = false;

   //Save the values for when (options & SEARCH_KEEP) is used.
   //(there is no "if ()" around this because gcc wants them initialized)
   old_off = prevSearchPatternsP[0].off;
   //position of the last match
   Pos pos = curPor->cursor;   //start searching at the cursor position

   //Find out the direction of the search.
   if (dirc == 0)
      dirc = prevSearchPatternsP[0].off.dir;
   else {
      prevSearchPatternsP[0].off.dir = dirc;
   }
   if (options & SEARCH_REV) {
      if (dirc == '/')
         dirc = '?';
      else
         dirc = '/';
   }

   //If the cursor is in a closed fold, don't find another match in the same fold.
   if (dirc == '/') {
      if (getFolds(pos.lnum, NULL, OUT &pos.lnum))
         pos.col = MAXCOL - 2;   //avoid overflow when adding 1
   } ei (getFolds(pos.lnum, OUT &pos.lnum, NULL))
      pos.col = 0;

   //Turn @hlsearch hiliting back on.
   if (!hiliteSearchG && !(options & SEARCH_KEEP)) {
      redraw_all_later(UPD_SOME_VALID);
      setHlsearch(true);
   }

   //Repeat the search when pattern followed by ';', e.g. "/foo/;?bar".
   for (;;) {
      int show_top_bot_msg = false;

      searchstr = pat;

      dircp = NULL;
                      //use previous pattern
      if (pat.len == 0 || pat.c[0] == search_delim) {
          if (prevSearchPatternsP[RE_SEARCH].pat.len == 0) {      //no previous pattern
            if (prevSearchPatternsP[RE_SUBST].pat.len == 0) {
               emsg(_(e_no_previous_regular_expression));
               retval = 0;
               goto end_do_search;
            }
            searchstr = prevSearchPatternsP[RE_SUBST].pat;
         } else {
            //make search_regcomp() use prevSearchPatternsP[RE_SEARCH].pat
            searchstr = (Text){null, 0};
         }
      }

      if (pat.len > 0) {  //look for (new) offset
         //Find end of regular expression. If there is a matching '/' or '?', toss it.
         ps = strcopy;
         p = skip_regexp_ex(pat.c, search_delim, true, &strcopy, NULL, NULL);
         if (strcopy != ps) {
            //made a copy of "pat" to change "\?" to "?"
            pat = text(strcopy);
            searchstr = (Text){strcopy, pat.len};
         }
         if (*p == search_delim) {
            searchstr.len = p - pat.c;
            dircp = p;   //remember where we put the ZERO
            *p++ = ZERO;
         }
         prevSearchPatternsP[0].off.line = false;
         prevSearchPatternsP[0].off.end = false;
         prevSearchPatternsP[0].off.off = 0;
         //Check for a line offset or a character offset.
         //For doGetCommandAddress (echo off) we don't check for a character
         //offset, because it is meaningless and the 's' could be a substitute command.
         if (*p == '+' || *p == '-' || EE_ISDIGIT(*p))
            prevSearchPatternsP[0].off.line = true;
         ei ((options & SEARCH_OPT) && (*p == 'e' || *p == 's' || *p == 'b')) {
            if (*p == 'e')      //end
                prevSearchPatternsP[0].off.end = SEARCH_END;
            ++p;
         }
         if (EE_ISDIGIT(*p) || *p == '+' || *p == '-') { //got an offset
                         //'nr' or '+nr' or '-nr'
            if (EE_ISDIGIT(*p) || EE_ISDIGIT(*(p + 1)))
               prevSearchPatternsP[0].off.off = atol((char *)p);
            ei (*p == '-')       //single '-'
               prevSearchPatternsP[0].off.off = -1;
            else             //single '+'
               prevSearchPatternsP[0].off.off = 1;
            ++p;
            while (EE_ISDIGIT(*p))       //skip number
               ++p;
          }

          pat.len -= p - pat.c;
          pat.c = p;             //put pat after search command
      }

      showSearchStats = false;
      if ((options & SEARCH_ECHO) && messaging() && !msg_silent && (!cmd_silent)) {
         Byte off_buf[40];
         Unt off_len = 0;
         Unt plen;
         Unt msgbufsize;

         //Compute msgRowG early.
         msg_start();

         //Get the offset, so we know how long it is.
         if (!cmd_silent &&
             (prevSearchPatternsP[0].off.line 
              || prevSearchPatternsP[0].off.end 
              || prevSearchPatternsP[0].off.off)
         ) {
            off_buf[off_len++] = dirc;
            if (prevSearchPatternsP[0].off.end)
                off_buf[off_len++] = 'e';
            ei (!prevSearchPatternsP[0].off.line)
                off_buf[off_len++] = 's';
            off_buf[off_len] = ZERO;
            if (prevSearchPatternsP[0].off.off != 0 || prevSearchPatternsP[0].off.line)
                off_len += eeSnprintf(off_buf + off_len,
                  sizeof(off_buf) - off_len, "%+ld", prevSearchPatternsP[0].off.off);
         }

         if (searchstr.len == ZERO) {
            p = prevSearchPatternsP[0].pat.c;
            plen = prevSearchPatternsP[0].pat.len;
         } else {
            p = searchstr.c;
            plen = searchstr.len;
         }

         if (cmd_silent) {
            //Reserve enough space for the search pattern + offset +
            //search stat.  Use all the space available, so that the
            //search state is right aligned.  If there is not enough space
            //msg_strtrunc() will shorten in the middle.
            if (msg_scrolled != 0 && !cmd_silent)
                //Use all the columns.
                msgbufsize = (int)(visibleRowsG - msgRowG) * visibleColsG - 1;
            else
                //Use up to 'showcmd' column.
                msgbufsize = (int)(visibleRowsG - msgRowG - 1) * visibleColsG + shownCommandColG - 1;
            if (msgbufsize < plen + off_len + SEARCH_STAT_BUF_LEN + 3)
                msgbufsize = plen + off_len + SEARCH_STAT_BUF_LEN + 3;
         } else
            //Reserve enough space for the search pattern + offset.
            msgbufsize = plen + off_len + 3;

         eeglFree(msgbuf);
         msgbuf = alloc(msgbufsize);
         memset(msgbuf, ' ', msgbufsize);
         msgbuflen = msgbufsize - 1;
         msgbuf[msgbuflen] = ZERO;
         //do not fill the msgbuf buffer, if cmd_silent is set, leave it
         //empty for the search_stat feature.
         if (!cmd_silent) {
            CS trunc;

            msgbuf[0] = dirc;

            if (utf_iscomposing(mb_ptr2char(p))) {
               //Use a space to draw the composing char on.
               msgbuf[1] = ' ';
               MEMMOVE(msgbuf + 2, p, plen);
            } else
               MEMMOVE(msgbuf + 1, p, plen);
            if (off_len > 0)
               MEMMOVE(msgbuf + plen + 1, off_buf, off_len);

            trunc = msg_strtrunc(msgbuf, true);
            if (trunc != NULL) {
               eeglFree(msgbuf);
               msgbuf = trunc;
               msgbuflen = STRLEN(msgbuf);
            }

             msg_outtrans(msgbuf);
             msg_clr_eos();
             msg_check();

             gotoCommline(false);
             out_flush();
             msg_nowait = true;       //don't wait for this message
         }

         showSearchStats = true;
      }

      //If there is a character offset, subtract it from the current
      //position, so we don't get stuck at "?pat?e+2" or "/pat/s-2".
      //Skip this if pos.col is near MAXCOL (closed fold).
      //This is not done for a line offset, because then we would not be vi compatible.
      if (!prevSearchPatternsP[0].off.line && prevSearchPatternsP[0].off.off && pos.col < MAXCOL - 2) {
         if (prevSearchPatternsP[0].off.off > 0) {
            for (c = prevSearchPatternsP[0].off.off; c; --c)
               if (decl(&pos) == -1)
                  break;
            if (c) {        //at start of buffer
               pos.lnum = 0;   //allow lnum == 0 here
               pos.col = MAXCOL;
            }
         } else {
            for (c = prevSearchPatternsP[0].off.off; c; ++c)
               if (incl(&pos) == -1)
                  break;
            if (c) {        //at end of buffer
               pos.lnum = curBook->mem.lineCount + 1;
               pos.col = 0;
            }
         }
      }

      //The actual search.
      c = searchit(
         curPor, curBook, &pos, NULL, dirc == '/' ? FORWARD : BACKWARD,
         searchstr, count, 
         prevSearchPatternsP[0].off.end 
            + (options & (SEARCH_KEEP + SEARCH_PEEK + SEARCH_HIS + SEARCH_MSG 
               + SEARCH_START + ((pat.len != 0 && pat.c[0] == ';') ? 0 : SEARCH_NOOF))
            ),
         RE_LAST, sia
      );

      if (dircp)
         *dircp = search_delim; //restore second '/' or '?' for normal_cmd()


      if (c == FAIL) {
         retval = 0;
         goto end_do_search;
      }
      if (prevSearchPatternsP[0].off.end && oap != NULL)
         oap->inclusive = true;  //'e' includes last character

      retval = 1;          //pattern found

      //Add character and/or line offset
      if ((options & SEARCH_NOOF) == 0 || (pat.len != 0 && pat.c[0] == ';')) {
         Pos org_pos = pos;

         if (prevSearchPatternsP[0].off.line){   //Add the offset to the line number.
            c = pos.lnum + prevSearchPatternsP[0].off.off;
            if (c < 1)
               pos.lnum = 1;
            ei (c > curBook->mem.lineCount)
               pos.lnum = curBook->mem.lineCount;
            else
               pos.lnum = c;
            pos.col = 0;

            retval = 2;       //pattern found, line offset added
         } ei (pos.col < MAXCOL - 2) {  //just in case
            //to the right, check for end of file
            c = prevSearchPatternsP[0].off.off;
            if (c > 0) {
               while (c-- > 0) {
                  if (incl(&pos) == -1)
                      break;
               } 
            } else {//to the left, check for start of file
               while (c++ < 0) {
                  if (decl(&pos) == -1)
                     break;
               } 
            }
         }
         if (!EQUAL_POS(pos, org_pos))
            has_offset = true;
      }

      //Show [1/15] if 'S' is not in 'shortmess'.
      if (showSearchStats) {
         cmdline_search_stat(
            dirc, &pos, &curPor->cursor, show_top_bot_msg, msgbuf, msgbuflen,
            (count != 1 || has_offset
                || (!(p_fdo & FDO_SEARCH) && getFolds(curPor->cursor.lnum, NULL, NULL))
            ),
            p_msc, SEARCH_STAT_DEF_TIMEOUT
         );
      } 

      //The search command can be followed by a ';' to do another search.
      //For example: "/pat/;/foo/+3;?bar"
      //This is like doing another search command, except:
      //- The remembered direction '/' or '?' is from the first search.
      //- When an error happens the cursor isn't moved at all.
      //Don't do this when called by doGetCommandAddress() (it handles ';' itself).
      if ((options & SEARCH_OPT) == 0 || pat.len == 0 || pat.c[0] != ';')
         break;

      dirc = pat.c[1];
      pat.c++;
      pat.len--;
      search_delim = dirc;
      if (dirc != '?' && dirc != '/') {
         retval = 0;
         emsg(_(e_expected_question_or_slash_after_semicolon));
         goto end_do_search;
      }
      pat.c++;
      pat.len--;
   }

   if (options & SEARCH_MARK)
      setpcmark();
   curPor->cursor = pos;
   curPor->setCursWant = true;

end_do_search:
   if ((options & SEARCH_KEEP) || (commModifierG.cmod_flags & CMOD_KEEPPATTERNS))
      prevSearchPatternsP[0].off = old_off;
   eeglFree(strcopy);
   eeglFree(msgbuf);

   return retval;
}

//search_for_exact_line(book, pos, dir, pat)
//
//Search for a line starting with the given pattern (ignoring leading
//white-space), starting from pos and going in direction "dir". "pos" will
//contain the position of the match found.    Blank lines match only if
//ADDING is set.  If p_ic is set then the pattern must be in lowercase.
//Return OK for success, or FAIL if no line found.
pub int
search_for_exact_line(
   Book* book,
   Pos* pos,
   int dir,
   CS pat
) {
   LineNr   start = 0;
   CS ptr;
   CS p;

   if (book->mem.lineCount == 0)
      return FAIL;
   for (;;) {
      pos->lnum += dir;
      if (pos->lnum < 1) {
         if (wrapSearchG) {
            pos->lnum = book->mem.lineCount;
         } else { 
            pos->lnum = 1;
            break;
         } 
      } ei (pos->lnum > book->mem.lineCount) {
         pos->lnum = 1;
         if (!wrapSearchG) {
            break;
         } 
      }
      if (pos->lnum == start)
         break;
      if (start == 0)
         start = pos->lnum;
      ptr = memGetLine(book, pos->lnum, false);
      p = skipwhite(ptr);
      pos->col = (ColNr) (p - ptr);

      //when adding lines the matching line may be empty but it is not
      //ignored because we are interested in the next line -- Acevedo
      if (compl_status_adding() && !compl_status_sol()) {
         if ((p_ic ? caseInsensitiveCompareMaxCol(p, pat) : STRCMP(p, pat)) == 0)
            return OK;
      } ei (*p != ZERO) {  //ignore empty lines
         //expanding lines or words
         if ((p_ic ? caseInsensitiveCompareNChars(p, pat, ins_compl_len())
                  : STRNCMP(p, pat, ins_compl_len())) == 0)
         return OK;
      }
   }
   return FAIL;
}

//Character Searches

//Search for a character in a line.  If "t_cmd" is false, move to the
//position of the character, otherwise move to just before the char.
//Do this "cap->count1" times. Return FAIL or OK.
pub int
searchc(ActionArg* cap, int t_cmd) {
   int c = cap->nchar;   //char to search for
   int dir = cap->arg;   //true for searching forward
   long count = cap->count1;   //repeat count
   int stop = true;

   if (c != ZERO) {  //normal search: remember args for repeat
      if (!keyWasStuffedG) {   //don't remember when redoing
         *lastc = c;
         set_csearch_direction(dir);
         set_csearch_until(t_cmd);
         lastc_bytelen = mb_char2bytes(c, lastc_bytes);
         if (cap->ncharC1 != 0) {
            lastc_bytelen += mb_char2bytes(cap->ncharC1, lastc_bytes + lastc_bytelen);
            if (cap->ncharC2 != 0)
               lastc_bytelen += mb_char2bytes(cap->ncharC2, lastc_bytes + lastc_bytelen);
          }
      }
   } else {     //repeat previous search
      if (*lastc == ZERO && lastc_bytelen <= 1)
         return FAIL;
      if (dir)   //repeat in opposite direction
         dir = -lastcdir;
      else
         dir = lastcdir;
      t_cmd = last_t_cmd;
      c = *lastc;
      //For multi-byte re-use last lastc_bytes[] and lastc_bytelen.

      //Force a move of at least one char, so ";" and "," will move the
      //cursor, even if the cursor is right in front of char we are looking at.
      if (count == 1 && t_cmd)
         stop = false;
   }

   if (dir == BACKWARD)
      cap->oper->inclusive = false;
   else
      cap->oper->inclusive = true;

   CS p = ml_get_curline();
   int col = curPor->cursor.col;
   int len = ml_get_curline_len();

   while (count--) {
      for (;;) {
         if (dir > 0) {
            col += utfCharLen(p + col);
            if (col >= len)
               return FAIL;
         } else {
            if (col == 0)
               return FAIL;
            col -= (*mb_head_off)(p, p + col - 1) + 1;
         }
         if (lastc_bytelen <= 1) {
            if (p[col] == c && stop)
            break;
         } ei (STRNCMP(p + col, lastc_bytes, lastc_bytelen) == 0 && stop)
            break;
         stop = true;
      }
   }

   if (t_cmd) {
      //backup to before the character (possibly double-byte)
      col -= dir;
      if (dir < 0)
         //Landed on the search char which is lastc_bytelen long
         col += lastc_bytelen - 1;
      else
         //To previous char, which may be multi-byte.
         col -= (*mb_head_off)(p, p + col);
   }
   curPor->cursor.col = col;

   return OK;
}

//"Other" Searches


//findmatch - find the matching paren or brace
pub Pos*
findmatch(Operator *oap, int initc) {
   return findmatchlimit(oap, initc, 0, 0);
}

//Return true if the character before "linep[col]" equals "ch".
//Return false if "col" is zero.
//Update "*prevcol" to the column of the previous character, unless "prevcol" is NULL.
//Handle multibyte string correctly.
private int
check_prevcol(
   CS linep,
   int      col,
   int      ch,
   int      *prevcol
) {
   --col;
   if (col > 0)
      col -= (*mb_head_off)(linep, linep + col);
   if (prevcol)
      *prevcol = col;
   return (col >= 0 && linep[col] == ch) ? true : false;
}

//Raw string start is found at linep[startpos.col - 1].
//Return true if the matching end can be found between startpos and endpos.
private int
find_rawstring_end(CS linep, Pos* startpos, Pos* endpos) {
   CS p;
   CS delim_copy;
   Unt delim_len;
   LineNr   lnum;
   int found = false;

   for (p = linep + startpos->col + 1; *p && *p != '('; ++p)
      {} 
   delim_len = (p - linep) - startpos->col - 1;
   delim_copy = copySubstr(linep + startpos->col + 1, delim_len);
   if (!delim_copy)
      return false;
   for (lnum = startpos->lnum; lnum <= endpos->lnum; ++lnum) {
      CS line = ml_get(lnum);

      for (p = line + (lnum == startpos->lnum ? startpos->col + 1 : 0); *p; ++p) {
         if (lnum == endpos->lnum && (ColNr)(p - line) >= endpos->col)
            break;
         if (*p == ')' && STRNCMP(delim_copy, p + 1, delim_len) == 0 && p[delim_len + 1] == '"') {
            found = true;
            break;
         }
      }
      if (found)
         break;
   }
   eeglFree(delim_copy);
   return found;
}

//Check matchpairs option for "*initc".
//If there is a match set "*initc" to the matching character and "*findc" to
//the opposite character.  Set "*backwards" to the direction.
//When "switchit" is true swap the direction.
private void
find_mps_values(
   OUT Unt* initc,
   OUT Unt* findc,
   OUT int* backwards,
   int switchit
) {
   if (!curBook->o.matchPairs)
      return;
      
   CS ptr = curBook->o.matchPairs;
   while (*ptr != ZERO) {
      CS prev;

      if (mb_ptr2char(ptr) == *initc) {
         if (switchit) {
             *findc = *initc;
             *initc = mb_ptr2char(ptr + utfCharLen(ptr) + 1);
             *backwards = true;
         } else {
             *findc = mb_ptr2char(ptr + utfCharLen(ptr) + 1);
             *backwards = false;
         }
         return;
      }
      prev = ptr;
      ptr += utfCharLen(ptr) + 1;
      if (mb_ptr2char(ptr) == *initc) {
         if (switchit) {
            *findc = *initc;
            *initc = mb_ptr2char(prev);
            *backwards = false;
         } else {
            *findc = mb_ptr2char(prev);
            *backwards = true;
         }
         return;
      }
      ptr += utfCharLen(ptr);
      if (*ptr == ',')
          ++ptr;
   }
}

//findmatchlimit -- find the matching paren or brace, if it exists within
//maxtravel lines of the cursor.  A maxtravel of 0 means search until falling
//off the edge of the file.
//
//"initc" is the character to find a match for.  ZERO means to find the
//character at or after the cursor. Special values:
//'*'  look for C-style comment / *
//'/'  look for C-style comment / *, ignoring comment-end
//'#'  look for preprocessor directives
//'R'  look for raw string start: R"delim(text)delim" (only backwards)
//
//flags: FM_BACKWARD   search backwards (when initc is '/', '*' or '#')
//   FM_FORWARD   search forwards (when initc is '/', '*' or '#')
//   FM_BLOCKSTOP   stop at start/end of block ({ or } in column 0)
//   FM_SKIPCOMM   skip comments (not implemented yet!)
//
//"oap" is only used to set oap->motion_type for a linewise motion, it can be NULL
pub Pos*
findmatchlimit(
   Operator* oap,
   Unt initc,
   int flags,
   int maxtravel
) {
   Unt findc = 0;      //matching brace
   Unt c;
   int count = 0;      //cumulative number of braces
   int backwards = false;   //init for gcc
   int raw_string = false;   //search for raw string
   int inquote = false;   //true when inside quotes
   CS ptr;
   int do_quotes;      //check for quotes in current line
   int at_start;      //do_quotes value at start position
   int hash_dir = 0;      //Direction searched for # things
   int comment_dir = 0;   //Direction searched for comments
   Pos match_pos;      //Where last slash-star was found
   int start_in_quotes;   //start position is in quotes
   int traveled = 0;      //how far we've searched so far
   int ignore_cend = false;    //ignore comment end
   int match_escaped = 0;   //search for escaped match
   int dir;         //Direction to search
   int comment_col = MAXCOL;   //start of / / comment
   static Pos pos;
   pos = curPor->cursor;         //current search position
   pos.coladd = 0;
   CS linep = ml_get(pos.lnum);//pointer to current line

   //Direction to search when initc is '/', '*' or '#'
   if (flags & FM_BACKWARD)
      dir = BACKWARD;
   ei (flags & FM_FORWARD)
      dir = FORWARD;
   else
      dir = 0;

   //if initc given, look in the table for the matching character
   //'/' and '*' are special cases: look for start or end of comment.
   //When '/' is used, we ignore running backwards into an star-slash, for
   //"[*" command, we just want to find any comment.
   if (initc == '/' || initc == '*' || initc == 'R') {
      comment_dir = dir;
      if (initc == '/')
         ignore_cend = true;
      backwards = (dir == FORWARD) ? false : true;
      raw_string = (initc == 'R');
      initc = ZERO;
   } ei (initc != '#' && initc != ZERO) {
      find_mps_values(OUT &initc, OUT &findc, OUT &backwards, true);
      if (dir)
         backwards = (dir == FORWARD) ? false : true;
      if (findc == ZERO)
         return NULL;
   } else {
      //Either initc is '#', or no initc was given and we need to look under the cursor.
      if (initc == '#') {
         hash_dir = dir;
      } else {
         //initc was not given, must look for something to match under or near the cursor.
         //Only check for special things when 'cpo' doesn't have '%'.
         //Are we before or at #if, #else etc.?
         ptr = skipwhite(linep);
         if (*ptr == '#' && pos.col <= (ColNr)(ptr - linep)) {
             ptr = skipwhite(ptr + 1);
             if (   STRNCMP(ptr, "if", 2) == 0
            || STRNCMP(ptr, "endif", 5) == 0
            || STRNCMP(ptr, "el", 2) == 0)
            hash_dir = 1;
         }

         //Are we on a comment?
         ei (linep[pos.col] == '/') {
            if (linep[pos.col + 1] == '*') {
               comment_dir = FORWARD;
               backwards = false;
               pos.col++;
            } ei (pos.col > 0 && linep[pos.col - 1] == '*') {
               comment_dir = BACKWARD;
               backwards = true;
               pos.col--;
            }
         } ei (linep[pos.col] == '*') {
            if (linep[pos.col + 1] == '/') {
               comment_dir = BACKWARD;
               backwards = true;
            } ei (pos.col > 0 && linep[pos.col - 1] == '/') {
               comment_dir = FORWARD;
               backwards = false;
            }
         }

         //If we are not on a comment or the # at the start of a line, then
         //look for brace anywhere on this line after the cursor.
         if (!hash_dir && !comment_dir) {
            //Find the brace under or after the cursor.
            //If beyond the end of the line, use the last character in the line.
            if (linep[pos.col] == ZERO && pos.col)
                --pos.col;
            for (;;) {
               initc = mb_ptr2char(linep + pos.col);
               if (initc == ZERO)
                  break;

               find_mps_values(&initc, &findc, &backwards, false);
               if (findc)
                  break;
               pos.col += utfCharLen(linep + pos.col);
            }
            if (!findc) {
               //no brace in the line, maybe use "  #if" then
               if (*skipwhite(linep) == '#')
                  hash_dir = 1;
               else
                  return NULL;
            } else {
               int col, bslcnt = 0;

               //Set "match_escaped" if there are an odd number of backslashes.
               for (col = pos.col; check_prevcol(linep, col, '\\', &col);)
                  bslcnt++;
               match_escaped = (bslcnt & 1);
            }
         }
      }
      if (hash_dir) {
         //Look for matching #if, #else, #elif, or #endif
         if (oap)
            oap->motion_type = MLINE;   //Linewise for this case only
         if (initc != '#') {
            ptr = skipwhite(skipwhite(linep) + 1);
            if (STRNCMP(ptr, "if", 2) == 0 || STRNCMP(ptr, "el", 2) == 0)
                hash_dir = 1;
            ei (STRNCMP(ptr, "endif", 5) == 0)
                hash_dir = -1;
            else
                return NULL;
         }
         pos.col = 0;
         while (!gotInterruptG) {
            if (hash_dir > 0) {
                if (pos.lnum == curBook->mem.lineCount)
               break;
            }
            ei (pos.lnum == 1)
                break;
            pos.lnum += hash_dir;
            linep = ml_get(pos.lnum);
            line_breakcheck();   //check for CTRL-C typed
            ptr = skipwhite(linep);
            if (*ptr != '#')
                continue;
            pos.col = (ColNr) (ptr - linep);
            ptr = skipwhite(ptr + 1);
            if (hash_dir > 0) {
               if (STRNCMP(ptr, "if", 2) == 0)
                  count++;
               ei (STRNCMP(ptr, "el", 2) == 0) {
                  if (count == 0)
                     return &pos;
               } ei (STRNCMP(ptr, "endif", 5) == 0) {
                  if (count == 0)
                     return &pos;
                  count--;
               }
            } else {
               if (STRNCMP(ptr, "if", 2) == 0) {
                  if (count == 0)
                      return &pos;
                  count--;
               } ei (initc == '#' && STRNCMP(ptr, "el", 2) == 0) {
                  if (count == 0)
                      return &pos;
               } ei (STRNCMP(ptr, "endif", 5) == 0)
                  count++;
            }
         }
         return NULL;
      }
   }

   do_quotes = -1;
   start_in_quotes = MAYBE;
   CLEAR_POS(&match_pos);

   //backward search: Check if this line contains a single-line comment
   if ((backwards && comment_dir))
      comment_col = check_linecomment(linep);

   while (!gotInterruptG) {
      //Go to the next position, forward or backward. We could use
      //inc() and dec() here, but that is much slower
      if (backwards) {
         //char to match is inside of comment, don't search outside
         if (pos.col == 0) {      //at start of line, go to prev. one
            if (pos.lnum == 1)   //start of file
               break;
            --pos.lnum;

            if (maxtravel > 0 && ++traveled > maxtravel)
               break;

            linep = ml_get(pos.lnum);
            pos.col = ml_get_len(pos.lnum); //pos.col on trailing ZERO
            do_quotes = -1;
            line_breakcheck();

            //Check if this line contains a single-line comment
            if (comment_dir)
                comment_col = check_linecomment(linep);
         } else {
            --pos.col;
            pos.col -= (*mb_head_off)(linep, linep + pos.col);
         } 
      } else { //forward search
          if (linep[pos.col] == ZERO
             //at end of line, go to next one
          ){
            if (pos.lnum == curBook->mem.lineCount)  //end of file
               //line is exhausted and comment with it,
               //don't search for match in code
               break;
            ++pos.lnum;

            if (maxtravel && traveled++ > maxtravel)
                break;

            linep = ml_get(pos.lnum);
            pos.col = 0;
            do_quotes = -1;
            line_breakcheck();
         } else {
            pos.col += utfCharLen(linep + pos.col);
         }
      }

      //If FM_BLOCKSTOP given, stop at a '{' or '}' in column 0.
      if (pos.col == 0 && (flags & FM_BLOCKSTOP)
                      && (linep[0] == '{' || linep[0] == '}')) {
         if (linep[0] == findc && count == 0)   //match!
            return &pos;
         break;               //out of scope
      }

      if (comment_dir) {
         //Note: comments do not nest, and we ignore quotes in them
         //TODO: ignore comment brackets inside strings
         if (comment_dir == FORWARD) {
            if (linep[pos.col] == '*' && linep[pos.col + 1] == '/') {
               pos.col++;
               return &pos;
            }
         } else  {//Searching backwards
            //A comment may contain / * or / /, it may also start or end
            //with / * /.   Ignore a / * after / / and after *.
            if (pos.col == 0)
               continue;
            ei (raw_string) {
               if (linep[pos.col - 1] == 'R'
                  && linep[pos.col] == '"'
                  && firstOccurrence(linep + pos.col + 1, '(') != NULL
               ) {
                  //Possible start of raw string. Now that we have the
                  //delimiter we can check if it ends before where we
                  //started searching, or before the previously found raw string start.
                  if (!find_rawstring_end(linep, &pos, count > 0 ? &match_pos : &curPor->cursor)) {
                     count++;
                     match_pos = pos;
                     match_pos.col--;
                  }
                  linep = ml_get(pos.lnum); //may have been released
               }
            } ei (   linep[pos.col - 1] == '/'
                  && linep[pos.col] == '*'
                  && (pos.col == 1 || linep[pos.col - 2] != '*')
                  && (int)pos.col < comment_col
            ) {
               count++;
               match_pos = pos;
               match_pos.col--;
            } ei (linep[pos.col - 1] == '*' && linep[pos.col] == '/') {
               if (count > 0)
                  pos = match_pos;
               ei (pos.col > 1 && linep[pos.col - 2] == '/' && (int)pos.col <= comment_col)
                  pos.col -= 2;
               ei (ignore_cend)
                  continue;
               else
                  return NULL;
               return &pos;
            }
         }
         continue;
      }

      //Braces inside of quotes are ignored, but only if there is an even number of quotes in the line
      if (do_quotes == -1) {
         //Count the number of quotes in the line, skipping \" and '"'. Watch out for "\\".
         at_start = do_quotes;
         for (ptr = linep; *ptr; ++ptr) {
            if (ptr == linep + pos.col + backwards)
               at_start = (do_quotes & 1);
            if (*ptr == '"' && (ptr == linep || ptr[-1] != '\'' || ptr[1] != '\''))
               ++do_quotes;
            if (*ptr == '\\' && ptr[1] != ZERO)
               ++ptr;
         }
         do_quotes &= 1;       //result is 1 with even number of quotes

         //If we find an uneven count, check current line and previous one for a '\' at the end.
         if (!do_quotes) {
            inquote = false;
            if (ptr[-1] == '\\') {
               do_quotes = 1;
               if (start_in_quotes == MAYBE) {
                  //Do we need to use at_start here?
                  inquote = true;
                  start_in_quotes = true;
               } ei (backwards)
                  inquote = true;
            }
            if (pos.lnum > 1) {
               ptr = ml_get(pos.lnum - 1);
               if (*ptr && *(ptr + ml_get_len(pos.lnum - 1) - 1) == '\\') {
                  do_quotes = 1;
                  if (start_in_quotes == MAYBE) {
                     inquote = at_start;
                     if (inquote)
                        start_in_quotes = true;
                  } ei (!backwards)
                      inquote = true;
               }

               //ml_get() only keeps one line, need to get linep again
               linep = ml_get(pos.lnum);
            }
         }
      }
      if (start_in_quotes == MAYBE)
         start_in_quotes = false;

      //If 'smartmatch' is set:
      //Things inside quotes are ignored by setting 'inquote'. If we find a quote without a 
      //preceding '\' invert 'inquote'. At the end of a line not ending in '\' we reset 'inquote'.
      //
      //In lines with an uneven number of quotes (without preceding '\') we do not know which part
      //to ignore. Therefore we only set inquote if the number of quotes in a line is even, unless 
      //this line or the previous one ends in a '\'.  Complicated, isn't it?
      c = mb_ptr2char(linep + pos.col);
      switch (c) {
      case ZERO:
         //at end of line without trailing backslash, reset inquote
         if (pos.col == 0 || linep[pos.col - 1] != '\\') {
            inquote = false;
            start_in_quotes = false;
         }
         break;

      case '"':
         //a quote that is preceded with an odd number of backslashes is ignored
         if (do_quotes) {
            int col;

            for (col = pos.col - 1; col >= 0; --col) {
               if (linep[col] != '\\')
                  break;
            } 
            if ((((int)pos.col - 1 - col) & 1) == 0) {
                inquote = !inquote;
                start_in_quotes = false;
            }
         }
         break;

      //If smart matching ('cpoptions' does not contain '%'):
      // Skip things in single quotes: 'x' or '\x'.  Be careful for single
      // single quotes, eg jon's.  Things like '\233' or '\x3f' are not
      // skipped, there is never a brace in them.
      // Ignore this when finding matches for `'.
      case '\'':
         if (initc != '\'' && findc != '\'') {
            if (backwards) {
               if (pos.col > 1) {
                  if (linep[pos.col - 2] == '\'') {
                     pos.col -= 2;
                     break;
                  } ei (linep[pos.col - 2] == '\\' && pos.col > 2 && linep[pos.col - 3] == '\'') {
                     pos.col -= 3;
                     break;
                  }
               }
            } ei (linep[pos.col + 1]) {  //forward search
               if (linep[pos.col + 1] == '\\' && linep[pos.col + 2] && linep[pos.col + 3] == '\'') {
                  pos.col += 3;
                  break;
               } ei (linep[pos.col + 2] == '\'') {
                  pos.col += 2;
                  break;
               }
            }
         }
         //FALLTHROUGH

      default:
         //Check for match outside of quotes, and inside of
         //quotes when the start is also inside of quotes.
         if ((!inquote || start_in_quotes == true) && (c == initc || c == findc)) {
            int   col, bslcnt = 0;

            for (col = pos.col; check_prevcol(linep, col, '\\', &col);) {
               bslcnt++;
            }
            if ((bslcnt & 1) == match_escaped) {
               if (c == initc)
                  count++;
               else {
                  if (count == 0)
                     return &pos;
                  count--;
               }
            }
         }
      }
   }

   if (comment_dir == BACKWARD && count > 0) {
      pos = match_pos;
      return &pos;
   }
   return (Pos *)NULL;   //never found it
}

//Check if line[] contains a / / comment. Return MAXCOL if not, otherwise return the column.
pub int
check_linecomment(CS line) {
   CS p = line;
   while ((p = firstOccurrence(p, '/')) != NULL) {
      //Accept a double /, unless it's preceded with * and followed by
      //*, because * / / * is an end and start of a C comment.  Only
      //accept the position if it is not inside a string.
      if (p[1] == '/' && (p == line || p[-1] != '*' || p[2] != '*')
                && !is_pos_in_string(line, (ColNr)(p - line))
      )
         break;
      ++p;
   }

   if (!p)
      return MAXCOL;
   return (int)(p - line);
}

//Check if the pattern is zero-width.
//If move is true, check from the beginning of the buffer, else from position "cur".
//"direction" is FORWARD or BACKWARD. Return true, false or -1 for failure.
private int
is_zero_width(
   Text pattern,
   Boole move,
   Pos* cur,
   Unt direction
) {
   RegMultilineMatch   regmatch;
   int nmatched = 0;
   int result = -1;
   Pos pos;
   int called_emsg_before = called_emsg;
   int flag = 0;

   if (pattern.len == 0) {
      pattern = prevSearchPatternsP[last_idx].pat;
   }

   if (search_regcomp(pattern, NULL, RE_SEARCH, RE_SEARCH, SEARCH_KEEP, OUT &regmatch) == FAIL)
      return -1;

   //init startcol correctly
   regmatch.startpos[0].col = -1;
   //move to match
   if (move) {
      CLEAR_POS(&pos);
   } else {
      pos = *cur;
      //accept a match at the cursor position
      flag = SEARCH_START;
   }

   if (searchit(curPor, curBook, &pos, NULL, direction, pattern, 1,
              SEARCH_KEEP + flag, RE_SEARCH, NULL) != FAIL
   ) {
      //Zero-width pattern should match somewhere, then we can check if
      //start and end are in the same position.
      do {
          regmatch.startpos[0].col++;
          nmatched = eeRegexec_multi(&regmatch, curPor, curBook,
                   pos.lnum, regmatch.startpos[0].col, NULL);
          if (nmatched != 0)
         break;
      } while (regmatch.regprog != NULL
         && direction == FORWARD ? regmatch.startpos[0].col < pos.col
                     : regmatch.startpos[0].col > pos.col);

      if (called_emsg == called_emsg_before) {
          result = (nmatched != 0
         && regmatch.startpos[0].lnum == regmatch.endpos[0].lnum
         && regmatch.startpos[0].col == regmatch.endpos[0].col);
      }
   }

   eeRegFree(regmatch.regprog);
   return result;
}

//Find next search match under cursor, cursor at end.
//Used while an operator is pending, and in Visual mode.
pub int
current_search(long   count, Boole forward) {  //true for forward, false for backward
   Pos start_pos;   //start position of the pattern match
   Pos end_pos;   //end position of the pattern match
   Pos pos;      //position after the pattern
   int i;
   int dir;
   int result;      //result of various function calls
   int flags = 0;
   Pos   save_VIsual = VIsual;

   //When searching forward and the cursor is at the start of the Visual
   //area, skip the first search backward, otherwise it doesn't move.
   int skip_first_backward = forward && VIsual_active && LT_POS(curPor->cursor, VIsual);

   Pos orig_pos = pos = curPor->cursor;   //position of the cursor at beginning
   if (VIsual_active) {
      if (forward)
         incl(&pos);
      else
         decl(&pos);
   }

   //Is the pattern is zero-width?, this time, don't care about the direction
   int zero_width = is_zero_width(prevSearchPatternsP[last_idx].pat, true, &curPor->cursor, FORWARD);
   if (zero_width == -1)
      return FAIL;  //pattern not found

   //The trick is to first search backwards and then search forward again, so that a match at the 
   //current cursor position will be correctly captured. When "forward" is false do it the other 
   //way around.
   for (i = 0; i < 2; i++) {
      if (forward) {
         if (i == 0 && skip_first_backward)
            continue;
         dir = i;
      } else
         dir = !i;

      flags = 0;
      if (!dir && !zero_width)
         flags = SEARCH_END;
      end_pos = pos;

      //wrapping should not occur in the first round
      if (i == 0)
         wrapSearchG = false;

      result = searchit(curPor, curBook, &pos, &end_pos,
         (dir ? FORWARD : BACKWARD),
         prevSearchPatternsP[last_idx].pat, (long) (i ? count : 1),
         SEARCH_KEEP | flags, RE_SEARCH, NULL);

      wrapSearchG = true;

      //First search may fail, but then start searching from the
      //beginning of the file (cursor might be on the search match)
      //except when Visual mode is active, so that extending the visual
      //selection works.
      if (i == 1 && !result){ //not found, abort
          curPor->cursor = orig_pos;
          if (VIsual_active)
         VIsual = save_VIsual;
          return FAIL;
      } ei (i == 0 && !result) {
          if (forward) {
            //try again from start of buffer
            CLEAR_POS(&pos);
         }  else {
            //try again from end of buffer
            //searching backwards, so set pos to last line and col
            pos.lnum = curPor->book->mem.lineCount;
            pos.col  = ml_get_len(curPor->book->mem.lineCount);
          }
      }
   }

   start_pos = pos;

   if (!VIsual_active)
      VIsual = start_pos;

   //put the cursor after the match
   curPor->cursor = end_pos;
   if (LT_POS(VIsual, end_pos) && forward) {
      if (skip_first_backward)
         //put the cursor on the start of the match
         curPor->cursor = pos;
      else
         //put the cursor on last character of match
         dec_cursor();
   } ei (VIsual_active && LT_POS(curPor->cursor, VIsual) && forward)
      curPor->cursor = pos;   //put the cursor on the start of the match
   VIsual_active = true;
   VIsual_mode = 'v';

   if (p_fdo & FDO_SEARCH && keyWasTypedG)
      foldOpenCursor();

   setmouse();
   drawCurBookLater(UPD_INVERTED);
   showmode();

   return OK;
}

//return true if line 'lnum' is empty or has white chars only.
pub int
linewhite(LineNr lnum) {
   CS p = skipwhite(ml_get(lnum));
   return (*p == ZERO);
}

//Add the search count "[3/19]" to "msgbuf". See update_search_stat() for other arguments.
private void
cmdline_search_stat(
   int dirc,
   Pos* pos,
   Pos* cursor_pos,
   int show_top_bot_msg,
   CS msgbuf,
   Unt msgbuflen,
   int recompute,
   int maxcount,
   long timeout
) {
   SearchFileStat stat;

   update_search_stat(dirc, pos, cursor_pos, &stat, recompute, maxcount, timeout);
   if (stat.cur <= 0)
      return;

   Byte t[SEARCH_STAT_BUF_LEN];
   Unt   len;

   if (stat.incomplete == 1)
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[?/??]");
   ei (stat.cnt > maxcount && stat.cur > maxcount)
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[>%d/>%d]", maxcount, maxcount);
   ei (stat.cnt > maxcount)
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[%d/>%d]", stat.cur, maxcount);
   else
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[%d/%d]", stat.cur, stat.cnt);

   if (show_top_bot_msg && len + 2 < SEARCH_STAT_BUF_LEN) {
      MEMMOVE(t + 2, t, len);
      t[0] = 'W';
      t[1] = ' ';
      len += 2;
   }

   if (len > msgbuflen)
      len = msgbuflen;
   MEMMOVE(msgbuf + msgbuflen - len, t, len);

   if (dirc == '?' && stat.cur == maxcount + 1)
      stat.cur = -1;

   //keep the message even after redraw, but don't put in history
   msg_hist_off = true;
   give_warning(msgbuf, false);
   msg_hist_off = false;
}

//Add the search count information to "stat". "stat" must not be NULL.
//When "recompute" is true always recompute the numbers.
//dirc == 0: don't find the next/previous match (only set the result to "stat")
//dirc == '/': find the next match
//dirc == '?': find the previous match
private void
update_search_stat(
   int dirc,
   Pos* pos,
   Pos* cursor_pos,
   SearchFileStat* stat,
   int recompute,
   int maxcount,
   long timeout
) {
   Pos p = (*pos);
   static Pos lastpos = {0, 0, 0};
   static int cur = 0;
   static int cnt = 0;
   static int exact_match = false;
   static int incomplete = 0;
   static int last_maxcount = 0;
   static int chgtick = 0;
   static CS lastpat = NULL;
   static Unt lastpatlen = 0;
   static Book* lBook = NULL;
   ProfTime  start;

   CLEAR_POINTER(stat);

   if (dirc == 0 && !recompute && !EMPTY_POS(lastpos)) {
      stat->cur = cur;
      stat->cnt = cnt;
      stat->exact_match = exact_match;
      stat->incomplete = incomplete;
      stat->last_maxcount = p_msc;
      return;
   }
   last_maxcount = maxcount;

   Boole wraparound = ((dirc == '?' && LT_POS(lastpos, p)) || (dirc == '/' && LT_POS(p, lastpos)));

   //If anything relevant changed the count has to be recomputed.
   if (!(chgtick == CHANGEDTICK(curBook)
         && (lastpat
             && STRNCMP(lastpat, prevSearchPatternsP[last_idx].pat.c, lastpatlen) == 0
             && lastpatlen == prevSearchPatternsP[last_idx].pat.len
         )
         && EQUAL_POS(lastpos, *cursor_pos)
         && lBook == curBook) 
      || wraparound || cur < 0
      || (maxcount > 0 && cur > maxcount) || recompute
   ) {
      cur = 0;
      cnt = 0;
      exact_match = false;
      incomplete = 0;
      CLEAR_POS(&lastpos);
      lBook = curBook;
   }

   //when searching backwards and having jumped to the first occurrence,
   //cur must remain greater than 1
   if (EQUAL_POS(lastpos, *cursor_pos) && !wraparound
         && (dirc == 0 || dirc == '/' ? cur < cnt : cur > 1))
      cur += dirc == 0 ? 0 : dirc == '/' ? 1 : -1;
   else {
      int done_search = false;
      Pos endpos = {0, 0, 0};

      wrapSearchG = false;
      if (timeout > 0)
         profile_setlimit(timeout, &start);
      while (!gotInterruptG 
            && searchit(
                  curPor, curBook, &lastpos, &endpos, FORWARD, (Text){null, 0}, 1, SEARCH_KEEP, RE_LAST, NULL
               ) != FAIL
      ) {
         done_search = true;
         //Stop after passing the time limit.
         if (timeout > 0 && profile_passed_limit(&start)) {
            incomplete = 1;
            break;
         }
         cnt++;
         if (LTOREQ_POS(lastpos, p)) {
            cur = cnt;
            if (LT_POS(p, endpos))
                exact_match = true;
         }
         fast_breakcheck();
         if (maxcount > 0 && cnt > maxcount) {
            incomplete = 2;    //max count exceeded
            break;
         }
      }
      if (gotInterruptG)
         cur = -1; //abort
      if (done_search) {
         eeglFree(lastpat);
         lastpat = 
            copySubstr(prevSearchPatternsP[last_idx].pat.c, prevSearchPatternsP[last_idx].pat.len);
         lastpatlen = prevSearchPatternsP[last_idx].pat.len;
         chgtick = CHANGEDTICK(curBook);
         lBook = curBook;
         lastpos = p;
      }
   }
   stat->cur = cur;
   stat->cnt = cnt;
   stat->exact_match = exact_match;
   stat->incomplete = incomplete;
   stat->last_maxcount = last_maxcount;
   wrapSearchG = true;
}

//Get line "lnum" and copy it into "buf[LSIZE]".
//The copy is made because the regexp may make the line invalid when using a mark.
private CS
get_line_and_copy(LineNr lnum, CS buf) {
   CS line = ml_get(lnum);
   copySubstrToAllocation(buf, (Text){line, LSIZE - 1});
   return buf;
}

//Find identifiers or defines in included files.
//If p_ic && compl_status_sol() then ptr must be in lowercase.
pub void
find_pattern_in_path(
   CS ptr,      //pointer to search pattern
   Unt dir,   //direction of expansion
   int len,      //length of search pattern
   int whole,      //match whole words only
   int skip_comments,   //don't match inside comments
   int type,      //Type of search; are we looking for a type? a macro?
   Long count,
   int action,      //What to do when we find it
   LineNr start_lnum,   //first line to start searching
   LineNr end_lnum,   //last line for searching
   int forceit,   //If true, always switch to the found path
   int silent      //Do not print messages when ACTION_EXPAND
){
   SearchedFile* bigger;      //When we need more space
   int      max_path_depth = 50;
   long   match_count = 1;

   CS pat;
   CS new_fname;
   CS curr_fname = curBook->currFileName;
   CS prev_fname = NULL;
   int      depth;
   int      depth_displayed;   //For type==CHECK_PATH
   int      old_files;
   int      already_searched;
   CS line;
   CS p;
   Byte   save_char;
   int      define_matched;
   RegMatch   regmatch;
   RegMatch   incl_regmatch;
   RegMatch   def_regmatch;
   int      matched = false;
   int      did_show = false;
   Boole      found = false;
   int      i;
   CS already = NULL;
   CS startp = NULL;
   CS inc_opt = NULL;
   Portal   *curPor_save = NULL;

   regmatch.regprog = NULL;
   incl_regmatch.regprog = NULL;
   def_regmatch.regprog = NULL;

   CS file_line = alloc(LSIZE);

   if (type != CHECK_PATH && type != FIND_DEFINE
       //when CONT_SOL is set compare "ptr" with the beginning of the
       //line is faster than quote_meta/regcomp/regexec "ptr" -- Acevedo
       && !compl_status_sol()
   ) {
      pat = alloc(len + 5);
      eeSnprintf(pat, len + 5, whole ? "\\<%.*s\\>" : "%.*s", len, ptr);
      //ignore case according to p_ic, p_scs and pat
      regmatch.rm_ic = ignorecase(pat);
      regmatch.regprog = compileRegexp(pat, RE_MAGIC);
      eeglFree(pat);
      if (regmatch.regprog == NULL)
          goto fpip_end;
   }
   inc_opt = curBook->o.includer;
   if (inc_opt) {
      incl_regmatch.regprog = compileRegexp(inc_opt, RE_MAGIC);
      if (incl_regmatch.regprog == NULL)
         goto fpip_end;
      incl_regmatch.rm_ic = false;   //don't ignore case in incl. pat.
   }
   if (type == FIND_DEFINE && curBook->o.definer) {
      def_regmatch.regprog = compileRegexp(curBook->o.definer, RE_MAGIC);
      if (!def_regmatch.regprog)
         goto fpip_end;
      def_regmatch.rm_ic = false;   //don't ignore case in define pat.
   }
   //Stack of included files
   Arr(SearchedFile) files = lallocZeroed(max_path_depth * sizeof(SearchedFile), true);
   old_files = max_path_depth;
   depth = depth_displayed = -1;

   LineNr lnum = start_lnum;
   if (end_lnum > curBook->mem.lineCount)
      end_lnum = curBook->mem.lineCount;
   if (lnum > end_lnum)      //do at least one line
      lnum = end_lnum;
   line = get_line_and_copy(lnum, file_line);

   for (;;) {
      if (incl_regmatch.regprog != NULL && eeRegexec(&incl_regmatch, line, (ColNr)0)){
         CS p_fname = (curr_fname == curBook->currFileName)
                        ? curBook->fullFileName : curr_fname;

         if (inc_opt != NULL && strstr((char *)inc_opt, "\\zs") != NULL)
            //Use text from '\zs' to '\ze' (or end) of 'include'.
            new_fname = find_file_name_in_path(incl_regmatch.startp[0],
                   (int)(incl_regmatch.endp[0] - incl_regmatch.startp[0]),
                   FNAME_EXP|FNAME_INCL|FNAME_REL, 1L, p_fname);
         else
            //Use text after match with 'include'.
            new_fname = file_name_in_line(incl_regmatch.endp[0], 0,
                    FNAME_EXP|FNAME_INCL|FNAME_REL, 1L, p_fname, NULL);
         already_searched = false;
         if (new_fname) {
            //Check whether we have already searched in this file
            for (i = 0;; i++) {
               if (i == depth + 1)
                  i = old_files;
               if (i == max_path_depth)
                  break;
               if (fullpathcmp(new_fname, files[i].name, true, true) & FPC_SAME) {
                  if (type != CHECK_PATH && action == ACTION_SHOW_ALL && files[i].matched) {
                     msg_putchar('\n');       //cursor below last one
                     if (!gotInterruptG)       //don't display if 'q'
                               //typed at "--more--" message
                      {
                        msg_home_replace_hl(new_fname);
                        msg_puts(_(" (includes previously listed match)"));
                        prev_fname = NULL;
                     }
                  }
                  EE_CLEAR(new_fname);
                  already_searched = true;
                  break;
               }
            }
         }

         if (type == CHECK_PATH 
               && (action == ACTION_SHOW_ALL || (new_fname == NULL && !already_searched))
         ) {
            if (did_show)
                msg_putchar('\n');       //cursor below last one
            else {
               gotoCommline(true);       //cursor at status line
               msg_puts_title(_("--- Included files "));
               if (action != ACTION_SHOW_ALL)
                  msg_puts_title(_("not found "));
               msg_puts_title(_("in path ---\n"));
            }
            did_show = true;
            while (depth_displayed < depth && !gotInterruptG) {
               ++depth_displayed;
               for (i = 0; i < depth_displayed; i++)
                  msg_puts(S"  ");
               msg_home_replace(files[depth_displayed].name);
               msg_puts(S" -->\n");
            }
            if (!gotInterruptG) { //don't display if 'q' typed for "--more--" message
               for (i = 0; i <= depth_displayed; i++)
                  msg_puts(S"  ");
               if (new_fname) {
                  //using "new_fname" is more reliable, e.g., when @includeexpr is set.
                  msgOuttransDeco(new_fname, getDecoFlags(HLF_D));
               } else {
                  //Isolate the file name. Include the surrounding "" or <> if present.
                  if (inc_opt != NULL && strstr((char *)inc_opt, "\\zs") != NULL) {
                     //pattern contains \zs, use the match
                     p = incl_regmatch.startp[0];
                     i = (int)(incl_regmatch.endp[0] - incl_regmatch.startp[0]);
                  } else {
                     //find the file name after the end of the match
                     for (p = incl_regmatch.endp[0]; *p && !eeIsFnameChar(*p); p++)
                        {}
                     for (i = 0; eeIsFnameChar(p[i]); i++)
                        {}
                  }

                  if (i == 0) {
                      //Nothing found, use the rest of the line.
                      p = incl_regmatch.endp[0];
                      i = (int)STRLEN(p);
                  }
                  //Avoid checking before the start of the line, can
                  //happen if \zs appears in the regexp.
                  ei (p > line) {
                     if (p[-1] == '"' || p[-1] == '<') {
                        --p;
                        ++i;
                     }
                     if (p[i] == '"' || p[i] == '>')
                        ++i;
                  }
                  save_char = p[i];
                  p[i] = ZERO;
                  msgOuttransDeco(p, getDecoFlags(HLF_D));
                  p[i] = save_char;
               }

               if (new_fname == NULL && action == ACTION_SHOW_ALL) {
                  if (already_searched)
                     msg_puts(_("  (Already listed)"));
                  else
                     msg_puts(_("  NOT FOUND"));
               }
            }
            out_flush();       //output each line directly
         }

         if (new_fname) {
            //Push the new file onto the file stack
            if (depth + 1 == old_files) {
               bigger = ALLOC_MULT(SearchedFile, max_path_depth * 2);
               for (i = 0; i <= depth; i++)
                   bigger[i] = files[i];
               for (i = depth + 1; i < old_files + max_path_depth; i++) {
                  bigger[i].fp = NULL;
                  bigger[i].name = NULL;
                  bigger[i].lnum = 0;
                  bigger[i].matched = false;
               }
               for (i = old_files; i < max_path_depth; i++)
                  bigger[i + max_path_depth] = files[i];
               old_files += max_path_depth;
               max_path_depth *= 2;
               eeglFree(files);
               files = bigger;
            }
            if ((files[depth + 1].fp = fopen((char *)new_fname, "r")) == NULL)
               eeglFree(new_fname);
            else {
               if (++depth == old_files) {
                  //lalloc() for 'bigger' must have failed above. We
                  //will forget one of our already visited files now.
                  eeglFree(files[old_files].name);
                  ++old_files;
               }
               files[depth].name = curr_fname = new_fname;
               files[depth].lnum = 0;
               files[depth].matched = false;
               if (action == ACTION_EXPAND && !silent) {
                  msg_hist_off = true;   //reset in msgTruncDeco()
                  eeSnprintf(IObuff, IOSIZE, _("Scanning included file: %s"), new_fname);
                  msgTruncDeco(IObuff, getDecoFlags(HLF_R));
               } ei (p_verbose >= 5) {
                  verbose_enter();
                  smsg(_("Searching included file %s"), (char *)new_fname);
                  verbose_leave();
               }

            }
         }
      } else {
         //Check if the line is a define (type == FIND_DEFINE)
         p = line;
   search_line:
         define_matched = false;
         if (def_regmatch.regprog != NULL && eeRegexec(&def_regmatch, line, (ColNr)0)) {
            //Pattern must be first identifier after 'define', so skip
            //to that position before checking for match of pattern.  Also
            //don't let it match beyond the end of this identifier.
            p = def_regmatch.endp[0];
            while (*p && !eeIsWordc(*p))
               p++;
            define_matched = true;
         }

         //Look for a match. Don't do this if we are looking for a
         //define and this line didn't match define_prog above.
         if (def_regmatch.regprog == NULL || define_matched) {
            if (define_matched || compl_status_sol()) {
               //compare the first "len" chars from "ptr"
               startp = skipwhite(p);
               if (p_ic)
                  matched = !caseInsensitiveCompareNChars(startp, ptr, len);
               else
                  matched = !STRNCMP(startp, ptr, len);
               if (matched && define_matched && whole && eeIsWordc(startp[len]))
                  matched = false;
            }
            ei (regmatch.regprog != NULL && eeRegexec(&regmatch, line, (ColNr)(p - line))) {
               matched = true;
               startp = regmatch.startp[0];
               //Check if the line is not a comment line (unless we are
               //looking for a define).  A line starting with "# define"
               //is not considered to be a comment line.
               if (!define_matched && skip_comments) {
                  if ((*line != '#' ||
                     STRNCMP(skipwhite(line + 1), "define", 6) != 0)
                     && get_leader_len(line, NULL, false, true))
                      matched = false;

                  //Also check for a "/ *" or "/ /" before the match.
                  //Skips lines like "int backwards;  / * normal index
                  //* /" when looking for "normal".
                  //Note: Doesn't skip "/ *" in comments.
                  p = skipwhite(line);
                  if (matched || (p[0] == '/' && p[1] == '*') || p[0] == '*') {
                     for (p = line; *p && p < startp; ++p) {
                        if (matched
                           && p[0] == '/'
                           && (p[1] == '*' || p[1] == '/')
                        ) {
                           matched = false;
                           //After "//" all text is comment
                           if (p[1] == '/')
                              break;
                            ++p;
                        } ei (!matched && p[0] == '*' && p[1] == '/') {
                            //Can find match after "* /".
                            matched = true;
                            ++p;
                        }
                     }
                  } 
               }
            }
         }
      }
      if (matched) {
         if (action == ACTION_EXPAND) {
            int   cont_s_ipos = false;
            int   add_r;

            if (depth == -1 && lnum == curPor->cursor.lnum)
               break;
            found = true;
            p = startp; 
            CS aux = p;
            if (compl_status_adding()) {
               p += ins_compl_len();
               if (eeIsWordPtr(p))
                  goto exit_matched;
               p = findWordStart(p);
            }
            p = find_word_end(p);
            i = (int)(p - aux);

            if (compl_status_adding() && i == ins_compl_len()) {
               //IOSIZE > compl_length, so the STRNCPY works
               STRNCPY(IObuff, aux, i);

               //Get the next line: when "depth" < 0  from the current buffer, otherwise from the 
               //included file. Jump to exit_matched when past the last line.
               if (depth < 0) {
                  if (lnum >= end_lnum)
                     goto exit_matched;
                  line = get_line_and_copy(++lnum, file_line);
               } ei (eeFgets(line = file_line, LSIZE, files[depth].fp))
                  goto exit_matched;

               //we read a line, set "already" to check this "line" later if depth >= 0 we'll 
               //increase files[depth].lnum far below  -- Acevedo
               already = aux = p = skipwhite(line);
               p = findWordStart(p);
               p = find_word_end(p);
               if (p > aux) {
                  if (*aux != ')' && IObuff[i-1] != TAB) {
                      if (IObuff[i-1] != ' ')
                         IObuff[i++] = ' ';
                      //IObuf =~ "\(\k\|\i\).* ", thus i >= 2
                  }
                  //copy as much as possible of the new word
                  if (p - aux >= IOSIZE - i)
                     p = aux + IOSIZE - i - 1;
                  STRNCPY(IObuff + i, aux, p - aux);
                  i += (int)(p - aux);
                  cont_s_ipos = true;
               }
               IObuff[i] = ZERO;
               aux = IObuff;

               if (i == ins_compl_len())
                  goto exit_matched;
            }

            add_r = ins_compl_add_infercase(
               aux, i, p_ic, curr_fname == curBook->currFileName ? NULL : curr_fname, dir, 
               cont_s_ipos, 0
            );
            if (add_r == OK)
               //if dir was BACKWARD then honor it just once
               dir = FORWARD;
            ei (add_r == FAIL)
               break;
          } ei (action == ACTION_SHOW_ALL) {
            found = true;
            if (!did_show)
               gotoCommline(true);      //cursor at status line
            if (curr_fname != prev_fname) {
               if (did_show)
                  msg_putchar('\n');   //cursor below last one
               if (!gotInterruptG)      //don't display if 'q' typed at "--more--" message
                  msg_home_replace_hl(curr_fname);
               prev_fname = curr_fname;
            }
            did_show = true;
            if (!gotInterruptG)
                show_pat_in_path(line, type, true, action,
                   (depth == -1) ? NULL : files[depth].fp,
                   (depth == -1) ? &lnum : &files[depth].lnum,
                   match_count++);

            //Set matched flag for this file and all the ones that
            //include it
            for (i = 0; i <= depth; ++i)
                files[i].matched = true;
         } ei (--count <= 0) {
            found = true;
            if (depth == -1 && lnum == curPor->cursor.lnum && g_do_tagpreview == 0)
               emsg(_(e_match_is_on_current_line));
            ei (action == ACTION_SHOW) {
               show_pat_in_path(
                  line, type, did_show, action, (depth == -1) ? NULL : files[depth].fp,
                   (depth == -1) ? &lnum : &files[depth].lnum, 1L
               );
               did_show = true;
            } else {
               //":psearch" uses the preview portal
               if (g_do_tagpreview != 0) {
                  curPor_save = curPor;
                  prepare_tagpreview(true, true, false);
               }
               if (action == ACTION_SPLIT) {
                  if (splitPortal(0, 0) == FAIL)
                     break;
                  curPor->o.diff = false;
               }
               if (depth == -1) {
                  //match in current file
                  if (g_do_tagpreview != 0) {
                     if (!portalIsValid(curPor_save))
                        break;
                     if (!GETFILE_SUCCESS(getfile(
                             curPor_save->book->fiNum, NULL, NULL, true, lnum, forceit
                          ))
                     )
                        break;   //failed to jump to file
                  } else
                     setpcmark();
                  curPor->cursor.lnum = lnum;
                  check_cursor();
                }
               else {
                  if (!GETFILE_SUCCESS(getfile(
                           0, files[depth].name, NULL, true, files[depth].lnum, forceit
                         )
                      )
                  )
                     break;   //failed to jump to file
                  //autocommands may have changed the lnum, we don't want that here
                  curPor->cursor.lnum = files[depth].lnum;
               }
            }
            if (action != ACTION_SHOW) {
               curPor->cursor.col = (ColNr)(startp - line);
               curPor->setCursWant = true;
            }

            if (g_do_tagpreview != 0 && curPor != curPor_save && portalIsValid(curPor_save)) {
               //Return cursor to where we were
               validate_cursor();
               redraw_later(UPD_VALID);
               enterPortal(curPor_save, true);
            } ei (PORTAL_IS_POPUP(curPor))
               //can't keep focus in popup portal
               enterPortal(firstPor, true);
            break;
         }
   exit_matched:
         matched = false;
         //look for other matches in the rest of the line if we are not at the end of it already
         if (def_regmatch.regprog == NULL
                && action == ACTION_EXPAND
                && !compl_status_sol()
                && *startp != ZERO
                && *(startp + utfCharLen(startp)) != ZERO
         )
            goto search_line;
      }
      line_breakcheck();
      if (action == ACTION_EXPAND)
         ins_compl_check_keys(30, false);
      if (gotInterruptG || ins_compl_interrupted())
         break;

      //Read the next line.  When reading an included file and encountering
      //end-of-file, close the file and continue in the file that included it.
      while (depth >= 0 && !already && eeFgets(line = file_line, LSIZE, files[depth].fp)) {
         fclose(files[depth].fp);
         --old_files;
         files[old_files].name = files[depth].name;
         files[old_files].matched = files[depth].matched;
         --depth;
         curr_fname = (depth == -1) ? curBook->currFileName : files[depth].name;
         if (depth < depth_displayed)
            depth_displayed = depth;
      }
      if (depth >= 0) {     //we could read the line
          files[depth].lnum++;
          //Remove any CR and LF from the line.
          i = (int)STRLEN(line);
          if (i > 0 && line[i - 1] == '\n')
         line[--i] = ZERO;
          if (i > 0 && line[i - 1] == '\r')
         line[--i] = ZERO;
      } ei (!already) {
          if (++lnum > end_lnum)
         break;
          line = get_line_and_copy(lnum, file_line);
      }
      already = NULL;
   }
   //End of big for (;;) loop.

   //Close any files that are still open.
   for (i = 0; i <= depth; i++) {
      fclose(files[i].fp);
      eeglFree(files[i].name);
   }
   for (i = old_files; i < max_path_depth; i++)
      eeglFree(files[i].name);
   eeglFree(files);

   if (type == CHECK_PATH) {
      if (!did_show) {
         if (action != ACTION_SHOW_ALL)
            msg(_("All included files were found"));
         else
            msg(_("No included files"));
      }
   } ei (!found && action != ACTION_EXPAND && !silent) {
      if (gotInterruptG || ins_compl_interrupted())
          emsg(_(e_interrupted));
      ei (type == FIND_DEFINE)
          emsg(_(e_couldnt_find_definition));
      else
          emsg(_(e_couldnt_find_pattern));
   }
   if (action == ACTION_SHOW || action == ACTION_SHOW_ALL)
      msg_end();

fpip_end:
   eeglFree(file_line);
   eeRegFree(regmatch.regprog);
   eeRegFree(incl_regmatch.regprog);
   eeRegFree(def_regmatch.regprog);
}

private void
show_pat_in_path(
   CS  line,
   int       type,
   int       did_show,
   int       action,
   FILE* fp,
   LineNr* lnum,
   long    count
) {
   CS p;
   Unt linelen;

   if (did_show)
      msg_putchar('\n');   //cursor below last one
   ei (!msg_silent)
      gotoCommline(true);   //cursor at status line
   if (gotInterruptG)      //'q' typed at "--more--" message
      return;
   linelen = STRLEN(line);
   for (;;) {
      p = line + linelen - 1;
      if (fp != NULL) {
          //We used fgets(), so get rid of newline at end
          if (p >= line && *p == '\n')
         --p;
          if (p >= line && *p == '\r')
         --p;
          *(p + 1) = ZERO;
      }
      if (action == ACTION_SHOW_ALL) {
          SPRINTF(IObuff, "%3ld: ", count);   //show match nr
          msg_puts(IObuff);
          SPRINTF(IObuff, FMT_UNT, *lnum);   //show line nr
                     //Highlight line numbers
          msgPutsDeco(IObuff, getDecoFlags(HLF_N));
          msg_puts(S" ");
      }
      msg_prt_line(line, false);
      out_flush();         //show one line at a time

      //Definition continues until line that doesn't end with '\'
      if (gotInterruptG || type != FIND_DEFINE || p < line || *p != '\\')
          break;

      if (fp) {
         if (eeFgets(line, LSIZE, fp)) //end of file
            break;
         linelen = STRLEN(line);
         ++*lnum;
      } else {
         if (++*lnum > curBook->mem.lineCount)
            break;
         line = ml_get(*lnum);
         linelen = ml_get_len(*lnum);
      }
      msg_putchar('\n');
   }
}

//Return the last used search pattern at "idx".
pub SearchPattern *
getPrevSearchPattern(int idx) {
   return &prevSearchPatternsP[idx];
}

//Return the last used search pattern index.
pub int
getPrevSearchOrSubstPattern(void) {
   return last_idx;
}

//"searchcount()" function
pub void
f_searchcount(Arr(Var) argvars, Var* returnVar) {
   Pos pos = curPor->cursor;
   CS pattern = NULL;
   int         maxcount = p_msc;
   long      timeout = SEARCH_STAT_DEF_TIMEOUT;
   int         recompute = true;
   SearchFileStat   stat;

   allocReturnDict(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      ListItem   *li;
      Boole error = false;

      if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
         return;
      Bag* dict = argvars[0].bag;
      DictItem* di = bagFind(dict, tConst("timeout"));
      if (di) {
         timeout = (long)varGetNumberChk(&di->c, OUT &error);
         if (error)
            return;
      }
      di = bagFind(dict, tConst("maxcount"));
      if (di) {
         maxcount = (int)varGetNumberChk(&di->c, OUT &error);
         if (error)
            return;
      }
      recompute = bagGetBool(dict, tConst("recompute"), recompute);
      di = bagFind(dict, tConst("pattern"));
      if (di) {
         pattern = convertVarToStringSingleUse(&di->c);
         if (pattern == NULL)
            return;
      }
      di = bagFind(dict, tConst(S"pos"));
      if (di) {
         if (di->c.tag != VAR_LIST) {
            showErrFmtMsg(_(e_invalid_argument_str), "pos");
            return;
         }
         if (list_len(di->c.list) != 3) {
            showErrFmtMsg(_(e_invalid_argument_str), "List format should be [lnum, col, off]");
            return;
         }
         li = list_find(di->c.list, 0L);
         if (li) {
            pos.lnum = varGetNumberChk(&li->c, OUT &error);
            if (error)
                return;
         }
         li = list_find(di->c.list, 1L);
         if (li) {
            pos.col = varGetNumberChk(&li->c, OUT &error) - 1;
            if (error)
                return;
         }
         li = list_find(di->c.list, 2L);
         if (li) {
            pos.coladd = varGetNumberChk(&li->c, OUT &error);
            if (error)
               return;
         }
      }
   }

   save_last_search_pattern();
   save_incsearch_state();
   if (pattern) {
      if (*pattern == ZERO)
         goto the_end;
      eeglFree(prevSearchPatternsP[last_idx].pat.c);
      prevSearchPatternsP[last_idx].pat = pattern 
         ? (Text){copyStr(pattern), STRLEN(pattern)} : (Text){null, 0};
   }
   if (prevSearchPatternsP[last_idx].pat.len == 0)
      goto the_end;   //the previous pattern was never defined

   update_search_stat(0, &pos, &pos, &stat, recompute, maxcount, timeout);

   bagAddNumber(returnVar->bag, S"current", stat.cur);
   bagAddNumber(returnVar->bag, S"total", stat.cnt);
   bagAddNumber(returnVar->bag, S"exact_match", stat.exact_match);
   bagAddNumber(returnVar->bag, S"incomplete", stat.incomplete);
   bagAddNumber(returnVar->bag, S"maxcount", stat.last_maxcount);

the_end:
   restore_last_search_pattern();
   restore_incsearch_state();
}

//}}}
//{{{ match hilitin'

# define SEARCH_HL_PRIORITY 0

//Add match to the match list of portal "po".
//If "pat" is not NULL the pattern will be hilited with the group "grp" with priority "prio".
//If "pos_list" is not NULL, the list of posisions defines the hilites. Optionally, a desired 
//ID "id" can be specified (greater than or equal to 1). If no particular ID is desired, -1 must
//be specified for "id". Return ID of added match, -1 on failure.
private int
match_add(
   Portal* po,
   CS grp,
   CS pat,
   int prio,
   int id,
   List* pos_list
) {
   MatchItem* cur;
   RegProg* regprog = NULL;

   if (*grp == ZERO || (pat && *pat == ZERO))
      return -1;
   if (id < -1 || id == 0) {
      showErrFmtMsg(_(e_invalid_id_nr_must_be_greater_than_or_equal_to_one_1), id);
      return -1;
   }
   
   Unt rtype = UPD_SOME_VALID;
   if (id == -1) {
      //use the next available match ID
      id = po->nextMatchId++;
   } else {
      //check the given ID is not already in use
      for (cur = po->firstMatch; cur; cur = cur->next) {
         if (cur->id == id) {
            showErrFmtMsg(_(e_id_already_taken_nr), id);
            return -1;
         }
      } 

      //Make sure the next match ID is always higher than the highest manually selected ID. Add 
      //some extra in case a few more IDs are added soon.
      if (po->nextMatchId < id + 100)
         po->nextMatchId = id + 100;
   }

   Short hiId;
   if ((hiId = syntaxClusterByName((Text){.c = grp, .len = STRLEN(grp)})) == 0) {
      showErrFmtMsg(_(e_no_such_highlight_group_name_str), grp);
      return -1;
   }
   if (pat && (regprog = compileRegexp(pat, RE_MAGIC)) == NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), pat);
      return -1;
   }

   //Build new match.
   MatchItem* m = ALLOC_CLEAR_ONE(MatchItem);
   if (pos_list && pos_list->len > 0) {
      m->pos = ALLOC_CLEAR_MULT(PosNoVirtLen, pos_list->len);
      m->posLen = pos_list->len;
   }
   m->id = id;
   m->priority = prio;
   m->pattern = pat ? copyStr(pat) : null;
   m->hiId = hiId;
   m->match.regprog = regprog;
   m->match.rmm_ic = false;
   m->match.rmm_maxcol = 0;

   //Set up position matches
   if (pos_list) {
      LineNr toplnum = 0;
      LineNr botlnum = 0;
      ListItem* li;
      CHECK_LIST_MATERIALIZE(pos_list);
      int i;
      for (i = 0, li = pos_list->first; li; i++, li = li->next) {
         LineNr lnum = 0;
         ColNr col = 0;
         int len = 1;
         List* subl;
         ListItem* subli;
         Boole error = false;

         if (li->c.tag == VAR_LIST) {
            subl = li->c.list;
            if (!subl)
               goto fail;
            subli = subl->first;
            if (!subli)
               goto fail;
            lnum = varGetNumberChk(&subli->c, OUT &error);
            if (error == true)
               goto fail;
            if (lnum == 0) {
               --i;
               continue;
            }
            m->pos[i].lnum = lnum;
            subli = subli->next;
            if (subli) {
               col = varGetNumberChk(&subli->c, OUT &error);
               if (error == true)
                  goto fail;
               subli = subli->next;
               if (subli) {
                  len = varGetNumberChk(&subli->c, OUT &error);
                  if (error == true)
                     goto fail;
               }
            }
            m->pos[i].col = col;
            m->pos[i].len = len;
         } ei (li->c.tag == VAR_NUMBER) {
            if (li->c.number == 0) {
                --i;
                continue;
            }
            m->pos[i].lnum = li->c.number;
            m->pos[i].col = 0;
            m->pos[i].len = 0;
         } else {
            emsg(_(e_list_or_number_required));
            goto fail;
         }
         if (toplnum == 0 || lnum < toplnum)
            toplnum = lnum;
         if (botlnum == 0 || lnum >= botlnum)
            botlnum = lnum + 1;
      }

      //Calculate top and bottom lines for redrawing area
      if (toplnum != 0) {
         redrawPortRangeLater(po, toplnum, botlnum);
         m->topLnum = toplnum;
         m->bottLnum = botlnum;
         rtype = UPD_VALID;
      }
   }

   //Insert new match.  The match list is in ascending order with regard to the match priorities.
   cur = po->firstMatch;
   MatchItem* prev = cur;
   while (cur && prio >= cur->priority) {
      prev = cur;
      cur = cur->next;
   }
   if (cur == prev)
      po->firstMatch = m;
   else
      prev->next = m;
   m->next = cur;

   redrawPortLater(po, rtype);
   return id;

fail:
   eeglFree(m->pattern);
   eeglFree(m->pos);
   eeglFree(m);
   return -1;
}

//Delete match with ID 'id' in the match list of portal 'po'.
//Print error messages if 'perr' is true.
private int
match_delete(Portal* po, int id, int perr) {
   if (id < 1) {
      if (perr == true)
         showErrFmtMsg(_(e_invalid_id_nr_must_be_greater_than_or_equal_to_one_2), id);
      return -1;
   }
   MatchItem* cur = po->firstMatch;
   MatchItem* prev = cur;
   for (; cur && cur->id != id; prev = cur, cur = cur->next) {
   }
   if (!cur) {
      if (perr == true)
         showErrFmtMsg(_(e_id_not_found_nr), id);
      return -1;
   }
   if (cur == prev)
      po->firstMatch = cur->next;
   else
      prev->next = cur->next;
   eeRegFree(cur->match.regprog);
   eeglFree(cur->pattern);
   
   Unt rtype = UPD_SOME_VALID;
   if (cur->topLnum != 0) {
      redrawPortRangeLater(po, cur->topLnum, cur->bottLnum);
      rtype = UPD_VALID;
   }
   eeglFree(cur->pos);
   eeglFree(cur);
   redrawPortLater(po, rtype);
   return 0;
}

//Delete all matches in the match list of portal 'po'.
pub void
clear_matches(Portal* po) {
   while (po->firstMatch) {
      MatchItem* m = po->firstMatch->next;
      eeRegFree(po->firstMatch->match.regprog);
      eeglFree(po->firstMatch->pattern);
      eeglFree(po->firstMatch->pos);
      eeglFree(po->firstMatch);
      po->firstMatch = m;
   }
   redrawPortLater(po, UPD_SOME_VALID);
}

//Get match from ID 'id' in portal 'po'. Return NULL if match not found.
private MatchItem*
get_match(Portal* po, int id) {
   MatchItem *cur;
   for (cur = po->firstMatch; cur && cur->id != id; cur = cur->next)
      {}
   return cur;
}

//Init for calling prepare_search_hl().
pub void
searchInitHilite(Portal* po, Match* search_hl) {
   //Setup for match and @hlsearch hiliting.  Disable any previous match
   MatchItem* cur = po->firstMatch;
   while (cur) {
      cur->mit_hl.rm = cur->match;
      if (cur->hiId == SHORT)
         cur->mit_hl.currHiId = SHORT;
      else
         cur->mit_hl.currHiId = cur->hiId;
      cur->mit_hl.book = po->book;
      cur->mit_hl.lnum = 0;
      cur->mit_hl.first_lnum = 0;
      cur = cur->next;
   }
   search_hl->book = po->book;
   search_hl->lnum = 0;
   search_hl->first_lnum = 0;
   //time limit is set at the toplevel, for all portals
}

//If there is a match fill "match" and return one. Return zero otherwise.
private int
next_search_hl_pos(
   OUT Match* match,   //points to a match
   LineNr lnum,
   MatchItem* matchItem,   //match item with positions
   ColNr mincol   //minimal column for a match
){
   int found = -1;
   for (int i = matchItem->currPos; i < matchItem->posLen; i++) {
      PosNoVirtLen* pos = &matchItem->pos[i];

      if (pos->lnum == 0)
         break;
      if (pos->len == 0 && pos->col < mincol)
         continue;
      if (pos->lnum == lnum) {
         if (found >= 0) {
            //if this match comes before the one at "found" then swap them
            if (pos->col < matchItem->pos[found].col) {
               PosNoVirtLen tmp = *pos;
               *pos = matchItem->pos[found];
               matchItem->pos[found] = tmp;
            }
         } else
            found = i;
      }
   }
   matchItem->currPos = 0;
   if (found >= 0) {
      ColNr start = matchItem->pos[found].col == 0 ? 0 : matchItem->pos[found].col - 1;
      ColNr end = matchItem->pos[found].col == 0 ? MAXCOL : start + matchItem->pos[found].len;

      match->lnum = lnum;
      match->rm.startpos[0].lnum = 0;
      match->rm.startpos[0].col = start;
      match->rm.endpos[0].lnum = 0;
      match->rm.endpos[0].col = end;
      match->is_addpos = true;
      match->has_cursor = false;
      matchItem->currPos = found + 1;
      return 1;
   }
   return 0;
}

//Search for a next 'hlsearch' or match.
//Uses match->buf.
//Sets match->lnum and match->rm contents.
//Note: Assumes a previous match is always before "lnum", unless match->lnum is zero.
//Careful: Any pointers for buffer lines will become invalid.
private void
next_search_hl(
   Portal* port,
   Match* search_hl,
   Match* match,   //points to search_hl or a match
   LineNr lnum,
   ColNr mincol,   //minimal column for a match
   MatchItem* cur   //to retrieve match positions if any
){
   ColNr matchcol;
   long nmatched;
   int called_emsg_before = called_emsg;
   int timed_out = false;

   //for :{range}s/pat only highlight inside the range
   if ((lnum < search_first_line || lnum > search_last_line) && cur == NULL) {
      match->lnum = 0;
      return;
   }

   if (match->lnum != 0) {
      //Check for three situations:
      //1. If the "lnum" is below a previous match, start a new search.
      //2. If the previous match includes "mincol", use it.
      //3. Continue after the previous match.
      LineNr l = match->lnum + match->rm.endpos[0].lnum - match->rm.startpos[0].lnum;
      if (lnum > l)
         match->lnum = 0;
      ei (lnum < l || match->rm.endpos[0].col > mincol)
         return;
   }

   //Repeat searching for a match until one is found that includes "mincol"
   //or none is found in this line.
   for (;;) {
      //Three situations:
      //1. No useful previous match: search from start of line.
      //2. Empty match: continue at next character.
      //   Break the loop if this is beyond the end of the line.
      if (match->lnum == 0)
         matchcol = 0;
      ei (match->rm.endpos[0].lnum == 0 && match->rm.endpos[0].col <= match->rm.startpos[0].col) {
         matchcol = match->rm.startpos[0].col;
         CS ml = memGetLine(match->book, lnum, false) + matchcol;
         if (*ml == ZERO) {
            ++matchcol;
            match->lnum = 0;
            break;
         }
         matchcol += utfCharLen(ml);
      } else
         matchcol = match->rm.endpos[0].col;

      match->lnum = lnum;
      if (match->rm.regprog != NULL) {
         //Remember whether match->rm is using a copy of the regprog in cur->match.
         int regprog_is_copy = (match != search_hl 
               && cur && match == &cur->mit_hl && cur->match.regprog == cur->mit_hl.rm.regprog);

         nmatched = eeRegexec_multi(&match->rm, port, match->book, lnum, matchcol, &timed_out);
         //Copy the regprog, in case it got freed and recompiled.
         if (regprog_is_copy)
            cur->match.regprog = cur->mit_hl.rm.regprog;

         if (called_emsg > called_emsg_before || gotInterruptG || timed_out) {
            //Error while handling regexp: stop using this regexp.
            if (match == search_hl) {
               //don't free regprog in the match list, it's a copy
               eeRegFree(match->rm.regprog);
               setHlsearch(false);
            }
            match->rm.regprog = NULL;
            match->lnum = 0;
            gotInterruptG = false;  //avoid the "Type :quit to exit Vim" message
            break;
         }
      } ei (cur)
         nmatched = next_search_hl_pos(match, lnum, cur, matchcol);
      else
         nmatched = 0;
      if (nmatched == 0) {
         match->lnum = 0;      //no match found
         break;
      }
      if (match->rm.startpos[0].lnum > 0
         || match->rm.startpos[0].col >= mincol
         || nmatched > 1
         || match->rm.endpos[0].col > mincol
      ) {
         match->lnum += match->rm.startpos[0].lnum;
         break;         //useful match found
      }
   }
}

//Advance to the match in portal "po" line "lnum" or past it.
pub void
prepare_search_hl(Portal* po, Match* search_hl, LineNr lnum) {
   Match* match;      //points to search_hl or a match
   Boole pos_inprogress;   //marks that position match search is in progress
   int n;

   //When using a multi-line pattern, start searching at the top
   //of the portal or just after a closed fold.
   //Do this both for search_hl and the match list.
   MatchItem* cur = po->firstMatch; //points to the match list
   Boole didHiliteSearch = PORTAL_IS_POPUP(po);  //skip search_hl in a popup portal
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false) {
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      if (match->rm.regprog && match->lnum == 0 && re_multiline(match->rm.regprog)) {
         if (match->first_lnum == 0) {
            for (match->first_lnum = lnum;
                 match->first_lnum > po->topLine; --match->first_lnum
            )
               if (getFoldsPortal(po, match->first_lnum - 1, NULL, NULL, true, NULL))
                  break;
         }
         if (cur)
            cur->currPos = 0;
         pos_inprogress = true;
         n = 0;
         while (match->first_lnum < lnum && (match->rm.regprog || (cur && pos_inprogress))) {
            next_search_hl(po, search_hl, match, match->first_lnum, (ColNr)n,
                            match == search_hl ? NULL : cur);
            pos_inprogress = !(!cur || cur->currPos == 0);
            if (match->lnum != 0) {
                match->first_lnum = match->lnum
                      + match->rm.endpos[0].lnum
                      - match->rm.startpos[0].lnum;
                n = match->rm.endpos[0].col;
            } else {
                ++match->first_lnum;
                n = 0;
            }
         }
      }
      if (match != search_hl && cur)
          cur = cur->next;
   }
}

//Update "match->has_cursor" based on the match in "match" and the cursor position.
private void
check_cur_search_hl(Portal* po, Match* match) {
   LineNr linecount = match->rm.endpos[0].lnum - match->rm.startpos[0].lnum;

   if (po->cursor.lnum >= match->lnum
         && po->cursor.lnum <= match->lnum + linecount
         && (po->cursor.lnum > match->lnum || po->cursor.col >= match->rm.startpos[0].col)
         && (po->cursor.lnum < match->lnum + linecount || po->cursor.col < match->rm.endpos[0].col)
   )
      match->has_cursor = true;
   else
      match->has_cursor = false;
}

//Prepare for 'hlsearch' and match hiliting in one portal line.
//Return true if there is such hiliting and set "searchHiId" to the current hilite decoration.
pub Boole
searchPrepareHiliteLine(
   Portal* po,
   LineNr lnum,
   ColNr mincol,
   OUT CS* line,
   Match* search_hl,
   OUT Short* searchHiId
){
   Boole areaHiliting = false;

   //Handle hiliting the last used search pattern and matches.
   //Do this for both search_hl and the match list. Do not use search_hl in a popup portal.
   MatchItem* cur = po->firstMatch; //points to the match list
   Boole didHiliteSearch = PORTAL_IS_POPUP(po); //whether search_hl has been processed or not
   Match* match; //search_hl or a match
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false) {
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      match->startcol = MAXCOL;
      match->endcol = MAXCOL;
      match->currHiId = SHORT;
      match->is_addpos = false;
      match->has_cursor = false;
      if (cur)
         cur->currPos = 0;
      next_search_hl(po, search_hl, match, lnum, mincol, match == search_hl ? NULL : cur);

      //Need to get the line again, a multi-line regexp may have made it invalid.
      *line = memGetLine(po->book, lnum, false);

      if (match->lnum != 0 && match->lnum <= lnum) {
         if (match->lnum == lnum)
            match->startcol = match->rm.startpos[0].col;
         else
            match->startcol = 0;
         if (lnum == match->lnum + match->rm.endpos[0].lnum - match->rm.startpos[0].lnum)
            match->endcol = match->rm.endpos[0].col;
         else
            match->endcol = MAXCOL;

         //check if the cursor is in the match before changing the columns
         if (match == search_hl)
            check_cur_search_hl(po, match);

         //Highlight one character for an empty match.
         if (match->startcol == match->endcol) {
            if ((*line)[match->endcol] != ZERO)
               match->endcol += utfCharLen((*line) + match->endcol);
            else
               match->endcol++;
         }
         if ((long)match->startcol < mincol) { //match at leftcol
            match->currHiId = match->currHiId;
            *searchHiId = match->currHiId;
         }
         areaHiliting = true;
      }
      if (match != search_hl && cur)
         cur = cur->next;
   }
   return areaHiliting;
}

//For a position in a line: Check for start/end of 'hlsearch' and other matches. After end, check 
//for start/end of next match. When another match, have to check for start again. Watch out for 
//matching an empty string! "onLastCol" is set to true with non-zero searchDeco and the next 
//column is endcol. Return the updated searchDeco.
pub Short
update_search_hl(
   Portal* po,
   LineNr lnum,
   ColNr col,
   OUT CS* line,
   Match* search_hl,
   int didLineDecorations,
   int lcs_eol_one,
   OUT Boole* onLastCol
) {
   Match* match;          //points to search_hl or a match
   Boole pos_inprogress;       //marks that position match search is in progress
   Short searchHiId = 0;

   //Do this for 'search_hl' and the match list (ordered by priority).
   MatchItem* cur = po->firstMatch; //the match list
   Boole didHiliteSearch = PORTAL_IS_POPUP(po); //whether search_hl has been processed or not
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false && (!cur || cur->priority > SEARCH_HL_PRIORITY)) {
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      if (cur)
         cur->currPos = 0;
      pos_inprogress = true;
      while (match->rm.regprog != NULL || (cur && pos_inprogress)) {
         if (match->startcol != MAXCOL && col >= match->startcol && col < match->endcol) {
            int next_col = col + utfCharLen(*line + col);

            if (match->endcol < next_col)
               match->endcol = next_col;
            match->currHiId = match->currHiId;
            //Hilite the match were the cursor is using the CurSearch group.
            if (match == search_hl && match->has_cursor) {
               match->extra = OVERLAY_DECO_INVERT;
            }
         } ei (col == match->endcol) {
            match->currHiId = SHORT;
            next_search_hl(po, search_hl, match, lnum, col, match == search_hl ? NULL : cur);
            pos_inprogress = (cur && cur->currPos != 0);

            //Need to get the line again, a multi-line regexp may have made it invalid.
            *line = memGetLine(po->book, lnum, false);

            if (match->lnum == lnum) {
               match->startcol = match->rm.startpos[0].col;
               if (match->rm.endpos[0].lnum == 0)
                  match->endcol = match->rm.endpos[0].col;
               else
                  match->endcol = MAXCOL;

               //check if the cursor is in the match
               if (match == search_hl)
                  check_cur_search_hl(po, match);

               if (match->startcol == match->endcol) {
                  //hilite empty match, try again after it
                  CS p = *line + match->endcol;

                  if (*p == ZERO)
                     //consistent with non-mbyte
                     match->endcol++;
                  else
                     match->endcol += utfCharLen(p);
               }

                //Loop to check if the match starts at the
                //current position
                continue;
            }
         }
         break;
      }
      if (match != search_hl && cur)
          cur = cur->next;
   }

   //Use decorations from match with highest priority among 'search_hl' and the match list.
   cur = po->firstMatch;
   didHiliteSearch = PORTAL_IS_POPUP(po);
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false && (cur == NULL || cur->priority > SEARCH_HL_PRIORITY)){
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      if (match->currHiId != SHORT) {
         searchHiId = match->currHiId;
         *onLastCol = col + 1 >= match->endcol;
      }
      if (match != search_hl && cur)
         cur = cur->next;
   }
   //Only highlight one character after the last column.
   if (*(*line + col) == ZERO && (didLineDecorations >= 1 || (po->o.list && lcs_eol_one == -1)))
      searchHiId = SHORT;
   return searchHiId;
}

pub int
get_prevcol_hl_flag(Portal* po, Match* search_hl, long curcol) {
   long prevcol = curcol;
   Boole prevcol_hl_flag = false;
   MatchItem* cur;         //points to the match list

   //don't do this in a popup portal
   if (portalIsPopup(po))
      return false;

   //we're not really at that column when skipping some text
   if ((long)(po->o.wrap ? po->skipCol : po->leftCol) > prevcol)
      ++prevcol;

   //Highlight a character after the end of the line if the match started
   //at the end of the line or when the match continues in the next line
   //(match includes the line break).
   if (!search_hl->is_addpos && (prevcol == (long)search_hl->startcol
      || (prevcol > (long)search_hl->startcol && search_hl->endcol == MAXCOL))
   )
      prevcol_hl_flag = true;
   else {
      cur = po->firstMatch;
      while (cur) {
         if (!cur->mit_hl.is_addpos && (prevcol == (long)cur->mit_hl.startcol
            || (prevcol > (long)cur->mit_hl.startcol && cur->mit_hl.endcol == MAXCOL))
         ){
            prevcol_hl_flag = true;
            break;
         }
         cur = cur->next;
      }
   }
   return prevcol_hl_flag;
}

//Get hiliting for the char after the text in "char_attr" from 'hlsearch' or match hiliting
pub void
get_search_match_hl(Portal* po, Match* search_hl, long col, OUT Short* charHiId) {
   MatchItem* cur = po->firstMatch;         //points to the match list
   Boole isPopup = PORTAL_IS_POPUP(po);  //flag to indicate whether search_hl has been processed or not
   Match* match; //points to search_hl or a match        
   while (cur || isPopup == false) {
      if (isPopup == false && ((cur && cur->priority > SEARCH_HL_PRIORITY) || !cur)){
         match = search_hl;
         isPopup = true;
      } else
         match = &cur->mit_hl;
      if (col - 1 == (long)match->startcol && (match == search_hl || !match->is_addpos))
         *charHiId = match->currHiId;
      if (match != search_hl && cur)
         cur = cur->next;
   }
}

private int
matchadd_dict_arg(Var* tv, OUT Portal** port) {
   DictItem* di;

   if (tv->tag != VAR_BAG) {
      emsg(_(e_dictionary_required));
      return FAIL;
   }


   if ((di = bagFind(tv->bag, tConst("window"))) == NULL)
      return OK;

   *port = portFindByNrOrId(&di->c);
   if (*port == NULL) {
      emsg(_(e_invalid_portal_number));
      return FAIL;
   }

   return OK;
}

pub void
f_clearmatches(Var* argvars, Var*) {
   Portal* port = getOptionalPortal(argvars, 0);
   if (port)
      clear_matches(port);
}

pub void
f_getmatches(Arr(Var) argvars, Var* returnVar) {
   Portal* port = getOptionalPortal(argvars, 0);
   if (!port)
      return;
      
   allocReturnList(returnVar);
   MatchItem* cur = port->firstMatch;
   while (cur) {
      Bag* bag = allocBag();
      if (!cur->match.regprog) {
         //match added with matchaddpos()
         for (int i = 0; i < cur->posLen; ++i) {
            Byte buf[30];  //use 30 to avoid compiler warning

            PosNoVirtLen* llpos = &cur->pos[i];
            if (llpos->lnum == 0)
               break;
            List* l = list_alloc();
            list_append_number(l, (Long)llpos->lnum);
            if (llpos->col > 0) {
               list_append_number(l, (Long)llpos->col);
               list_append_number(l, (Long)llpos->len);
            }
            SPRINTF(buf, S"pos%d", i + 1);
            bagAddList(bag, buf, l);
         }
      } else {
         bagAddString(bag, S"pattern", cur->pattern);
      }
      bagAddString(bag, S"group", syn_id2name(cur->hiId));
      bagAddNumber(bag, S"priority", (long)cur->priority);
      bagAddNumber(bag, S"id", (long)cur->id);
      listAppendBag(returnVar->list, bag);
      cur = cur->next;
   }
}

pub void
f_setmatches(Arr(Var) argvars, Var* returnVar) {
   List   *l;
   ListItem   *li;
   Bag   *d;
   List   *s = NULL;
   returnVar->number = -1;

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;
   Portal* port = getOptionalPortal(argvars, 1);
   if (!port)
      return;

   if ((l = argvars[0].list) != NULL) {
      //To some extent make sure that we are dealing with a list from "getmatches()".
      li = l->first;
      while (li) {
         if (li->c.tag != VAR_BAG || (d = li->c.bag) == NULL) {
            emsg(_(e_invalid_argument));
            return;
         }
         if (!(bagHasKey(d, tConst("group"))
            && (bagHasKey(d, tConst("pattern")) || bagHasKey(d, tConst("pos1")))
            && bagHasKey(d, tConst("priority"))
            && bagHasKey(d, tConst("id")))
         ) {
            emsg(_(e_invalid_argument));
            return;
         }
         li = li->next;
      }

      clear_matches(port);
      li = l->first;
      while (li) {
         int i = 0;
         Byte buf[30];  //use 30 to avoid compiler warning
         DictItem  *di;
         CS group;
         int priority;
         int id;

         d = li->c.bag;
         if (!bagHasKey(d, tConst("pattern"))) {
            if (!s) {
               s = list_alloc();
            }

            //match from matchaddpos()
            for (i = 1; i < 9; i++) {
               sprintf((char *)buf, (char *)"pos%d", i);
               if ((di = bagFind(d, mbText(buf))) != NULL) {
                  if (di->c.tag != VAR_LIST)
                      return;

                  list_append_tv(s, &di->c);
                  s->refCount++;
               } else
                  break;
            }
         }

         group = bagGetString(d, tConst("group"), true);
         priority = (int)bagGetNumber(d, tConst("priority"));
         id = (int)bagGetNumber(d, tConst("id"));
         if (i == 0) {
            match_add(port, group, bagGetString(d, tConst("pattern"), false), priority, id, NULL);
         } else {
            match_add(port, group, NULL, priority, id, s);
            list_unref(s);
            s = NULL;
         }
         eeglFree(group);

         li = li->next;
      }
      returnVar->number = 0;
   }
}

pub void
f_matchadd(Arr(Var) argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   int prio = 10;   //default priority
   int id = -1;
   Boole error = false;
   Portal* port = curPor;

   returnVar->number = -1;

   CS grp = convertVarToString(&argvars[0], buf);   //group
   CS pat = convertVarToString(&argvars[1], buf);   //pattern
   if (grp == NULL || pat == NULL)
      return;
   if (argvars[2].tag != VAR_UNKNOWN) {
      prio = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (argvars[3].tag != VAR_UNKNOWN) {
         id = (int)varGetNumberChk(argvars + 3, OUT &error);
         if (argvars[4].tag != VAR_UNKNOWN
               && matchadd_dict_arg(&argvars[4], &port) == FAIL)
            return;
      }
   }
   if (error == true)
      return;
   if (id >= 1 && id <= 3) {
      showErrFmtMsg(_(e_id_is_reserved_for_match_nr), id);
      return;
   }

   returnVar->number = match_add(port, grp, pat, prio, id, NULL);
}

//"matchaddpos()" function
pub void
f_matchaddpos(Arr(Var) argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   int prio = 10;
   int id = -1;
   Boole error = false;
   Portal* port = curPor;
   returnVar->number = -1;

   CS group = convertVarToString(&argvars[0], buf);
   if (group == NULL)
      return;

   if (argvars[1].tag != VAR_LIST) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list), "matchaddpos()");
      return;
   }
   List* l = argvars[1].list;
   if (!l || l->len == 0)
      return;

   if (argvars[2].tag != VAR_UNKNOWN) {
      prio = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (argvars[3].tag != VAR_UNKNOWN) {
         id = (int)varGetNumberChk(argvars + 3, OUT &error);

         if (argvars[4].tag != VAR_UNKNOWN && matchadd_dict_arg(&argvars[4], &port) == FAIL)
            return;
      }
   }
   if (error == true)
      return;

   //id == 3 is ok because matchaddpos() is supposed to substitute :3match
   if (id == 1 || id == 2) {
      showErrFmtMsg(_(e_id_is_reserved_for_match_nr), id);
      return;
   }

   returnVar->number = match_add(port, group, NULL, prio, id, l);
}

pub void
f_matcharg(Var* argvars, Var* returnVar) {
   allocReturnList(returnVar);

   MatchItem *m;

   int id = (int)tv_get_number(&argvars[0]);
   if (id >= 1 && id <= 3) {
      if ((m = get_match(curPor, id)) != NULL) {
         list_append_string(returnVar->list, syn_id2name(m->hiId), -1);
         list_append_string(returnVar->list, m->pattern, -1);
      } else {
         list_append_string(returnVar->list, NULL, -1);
         list_append_string(returnVar->list, NULL, -1);
      }
   }
}

pub void
f_matchdelete(Arr(Var) argvars, Var* returnVar) {
   Portal* port = getOptionalPortal(argvars, 1);
   if (!port)
      returnVar->number = -1;
   else
      returnVar->number = match_delete(port, (int)tv_get_number(&argvars[0]), true);
}

//":[N]match {group} {pattern}"
//called when skipping commands to find the next command.
pub void
c_match(Invocation* invo) {
   CS g = NULL;
   int c;
   int id;

   if (invo->line2 <= 3)
      id = invo->line2;
   else {
      emsg(_(e_invalid_command));
      return;
   }

   //First clear any old pattern.
   if (!invo->skip)
      match_delete(curPor, id, false);

   CS end;
   if (endsComm(invo->arg))
      end = invo->arg;
   ei ((STRNICMP(invo->arg, "none", 4) == 0
         && (SPACE_OR_TAB(invo->arg[4]) || endsComm(invo->arg + 4))))
      end = invo->arg + 4;
   else {
      CS p = skiptowhite(invo->arg);
      if (!invo->skip)
         g = copySubstr(invo->arg, p - invo->arg);
      p = skipwhite(p);
      if (*p == ZERO) {
         //There must be two arguments.
         eeglFree(g);
         showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
         return;
      }
      end = skip_regexp(p + 1, *p, true);
      if (!invo->skip) {
         if (*end != ZERO && !endsComm(skipwhite(end + 1))) {
            eeglFree(g);
            invo->errmsg = ex_errmsg(e_trailing_characters_str, end);
            return;
         }
         if (*end != *p) {
            eeglFree(g);
            showErrFmtMsg(_(e_invalid_argument_str), p);
            return;
         }

         c = *end;
         *end = ZERO;
         match_add(curPor, g, p + 1, 10, id, NULL);
         eeglFree(g);
         *end = c;
      }
   }
}

//}}}
//{{{help file searchin'

//":help": open a read-only portal on a help file
pub void
c_help(Invocation* invo) {
   CS arg;
   int      n;
   int      empty_fnum = 0;
   int      alt_fnum = 0;
   int len;
   int old_keyWasTypedG = keyWasTypedG;

   if (portErrorIfPopup(true))
      return;

   if (invo) {
      //A ":help" command ends at the first LF
      for (arg = invo->arg; *arg; ++arg) {
          if (*arg == '\n' || *arg == '\r') {
            *arg++ = ZERO;
            break;
         }
      }
      arg = invo->arg;

      if (invo->forceit && *arg == ZERO && curBook->kind != BOOK_HELP) {
          emsg(_(e_dont_panic));
          return;
      }

      if (invo->skip)       //not executing commands
          return;
   } else
      arg = S"";

   //remove trailing blanks
   CS p = arg + STRLEN(arg) - 1;
   while (p > arg && SPACE_OR_TAB(*p) && p[-1] != '\\') {
      *p-- = ZERO;
   }

   //Check for a specified language
   CS lang = check_help_lang(arg);

   //When no argument given go to the index.
   if (*arg == ZERO)
      arg = S"help.txt";

   //Check if there is a match for the argument.
   ExpandMatch matches = {};
   matches.a = createArena();
   n = find_help_tags(arg, invo && invo->forceit, OUT &matches);

   Unt i = 0;
   if (n != FAIL && lang != NULL) {
      //Find first item with the requested language.
      for (i = 0; i < matches.len; ++i) {
         len = (int)STRLEN(matches.c[i]);
         if (len > 3 
           && matches.c[i][len - 3] == '@'
           && caseInsensitiveCompare(matches.c[i] + len - 2, lang) == 0
         ) {
            break;
         }
      }
   } 
   if (i >= matches.len || n == FAIL) {
      if (lang)
         showErrFmtMsg(_(e_sorry_no_str_help_for_str), lang, arg);
      else
         showErrFmtMsg(_(e_sorry_no_help_for_str), arg);
      deleteArena(matches.a);
      return;
   }

   //The first match (in the requested language) is the best match.
   CS tag = copyStr(matches.c[i]);

   //Re-use an existing help portal or open a new one.
   //Always open a new one for ":tab help".
   if (!bookIsHelp(curPor->book) || commModifierG.cmod_tab != 0) {
   
      Portal   *po;
      if (commModifierG.cmod_tab != 0) {
         po = null;
      } else {
         FOR_ALL_PORTALS(po) {
            if (bookIsHelp(po->book)) {
               break;
            }
         } 
      }
      if (po && po->book->countPortals > 0)
          enterPortal(po, true);
      else {
         //There is no help portal yet. Try to open the file specified by the "helpfile" option.
         FILE* helpfd;   //file descriptor of help file
         if ((helpfd = FOPEN(MAIN_HELPFILE, READBIN)) == NULL) {
            smsg(_("Sorry, help file \"%s\" not found"), MAIN_HELPFILE);
            goto erret;
         }
         fclose(helpfd);

         //Split off help portal; put it at far top if no position
         //specified, the current portal is vertically split and narrow.
         n = WSP_HELP;
         if (commModifierG.cmod_split == 0 && curPor->width != visibleColsG && curPor->width < 80)
            n |= p_sb ? WSP_BOT : WSP_TOP;
         if (splitPortal(0, n) == FAIL)
            goto erret;

         if (curPor->height < p_hh)
            portSetHeight((int)p_hh, curPor);

         //Open help file (startEditingFile() will set kind = BOOK_HELP, readfile() will
         //set readonly flag). Set the alternate file to the previously edited file.
         alt_fnum = curBook->fiNum;
         (void)startEditingFile(0, NULL, NULL, NULL, ECMD_LASTL,
              ECMD_HIDE + ECMD_SET_HELP,
              NULL);  //buffer is still open, don't store info
         if ((commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
            curPor->altFnum = alt_fnum;
         empty_fnum = curBook->fiNum;
      }
   }

   restart_edit = 0;       //don't want insert mode in help file

   //Restore keyWasTypedG, setting 'filetype=help' may reset it.
   //It is needed for do_tag top open folds under the cursor.
   keyWasTypedG = old_keyWasTypedG;

   if (tag != NULL)
      do_tag(tag, DT_HELP, 1, false, true);

   //Delete the empty book if we're not using it.  Careful: autocommands
   //may have jumped to another portal, check that the book is not in a portal.
   if (empty_fnum != 0 && curBook->fiNum != empty_fnum) {
      Book* book = bookFindFileByBookNr(empty_fnum);
      if (book && book->countPortals == 0)
          bookWipe(book, true);
   }

   //keep the previous alternate file
   if (alt_fnum != 0 && curPor->altFnum == empty_fnum && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
      curPor->altFnum = alt_fnum;

erret:
   deleteArena(matches.a);
   eeglFree(tag);
}

//":helpclose": Close one help portal
pub void
c_helpclose(Invocation*) {
   Portal *port;

   FOR_ALL_PORTALS(port) {
      if (bookIsHelp(port->book)) {
         closePortal(port, false);
         return;
      }
   }
}

//In an argument search for a language specifiers in the form "@xx".
//Change the "@" to ZERO if found, and return a pointer to "xx". NULL if not found.
pub CS
check_help_lang(CS arg) {
   int len = (int)STRLEN(arg);

   if (len >= 3 && arg[len - 3] == '@' 
       && ASCII_ISALPHA(arg[len - 2]) && ASCII_ISALPHA(arg[len - 1])
   ){
      arg[len - 3] = ZERO;      //remove the '@'
      return arg + len - 2;
   }
   return NULL;
}

//Return a heuristic indicating how well the given string matches. The
//smaller the number, the better the match. This is the order of priorities,
//from best match to worst match:
// - Match with least alphanumeric characters is better.
// - Match with least total characters is better.
// - Match towards the start is better.
// - Match starting with "+" is worse (feature instead of command)
//Assumption is made that the matched_string passed has already been found to
//match some string for which help is requested.  webb.
pub int
help_heuristic(
   CS matched_string,
   int offset,         //offset for match
   int wrong_case      //no matching case
){
   int num_letters = 0;
   CS p;
   for (p = matched_string; *p; p++) {
      if (ASCII_ISALNUM(*p))
          num_letters++;
   } 

   //Multiply the number of letters by 100 to give it a much bigger
   //weighting than the number of characters.
   //If there only is a match while ignoring case, add 5000.
   //If the match starts in the middle of a word, add 10000 to put it somewhere in the last half.
   //If the match is more than 2 chars from the start, multiply by 200 to
   //put it after matches at the start.
   if (ASCII_ISALNUM(matched_string[offset]) && offset > 0
             && ASCII_ISALNUM(matched_string[offset - 1])
   )
      offset += 10000;
   ei (offset > 2)
      offset *= 200;
   if (wrong_case)
      offset += 5000;
   //Features are less interesting than the subjects themselves, but "+" alone is not a feature.
   if (matched_string[0] == '+' && matched_string[1] != ZERO)
      offset += 100;
   return (int)(100 * num_letters + STRLEN(matched_string) + offset);
}

//Compare functions for qsort() below, that checks the help heuristics number
//that has been put after the tagname by find_tags().
private int
helpCompare(const void *s1, const void *s2) {
   CS p1 = *(Byte **)s1 + strlen(*(char**)s1) + 1;
   CS p2 = *(Byte **)s2 + strlen(*(char**)s2) + 1;

   //Compare by help heuristic number first.
   int cmp = STRCMP(p1, p2);
   if (cmp != 0)
      return cmp;

   //Compare by strings as tie-breaker when same heuristic number.
   return strcmp(*(char **)s1, *(char **)s2);
}

//Find all help tags matching "arg", sort them and return in matches[], with
//the number of matches in num_matches.
//The matches will be sorted with a "best" match algorithm.
//When "keep_lang" is true try keeping the language of the current buffer.
pub int
find_help_tags(
   CS arg,
   int keep_lang,
   OUT ExpandMatch* matches
) {
   Byte   *s, *d;
   int      i;
   //Specific tags that either have a specific replacement or won't go
   //through the generic rules.
   static char *(except_tbl[][2]) = {
      {"*",      "star"},
      {"g*",      "gstar"},
      {"[*",      "[star"},
      {"]*",      "]star"},
      {":*",      ":star"},
      {"/*",      "/star"},
      {"/\\*",   "/\\\\star"},
      {"\"*",      "quotestar"},
      {"**",      "starstar"},
      {"cpo-*",   "cpo-star"},
      {"/\\(\\)",   "/\\\\(\\\\)"},
      {"/\\%(\\)",   "/\\\\%(\\\\)"},
      {"?",      "?"},
      {"??",      "??"},
      {":?",      ":?"},
      {"?<CR>",   "?<CR>"},
      {"g?",      "g?"},
      {"g?g?",   "g?g?"},
      {"g??",      "g??"},
      {"-?",      "-?"},
      {"q?",      "q?"},
      {"v_g?",   "v_g?"},
      {"/\\?",   "/\\\\?"},
      {"/\\z(\\)",   "/\\\\z(\\\\)"},
      {"\\=",      "\\\\="},
      {":s\\=",   ":s\\\\="},
      {"[count]",   "\\[count]"},
      {"[quotex]",   "\\[quotex]"},
      {"[range]",   "\\[range]"},
      {":[range]",   ":\\[range]"},
      {"[pattern]",   "\\[pattern]"},
      {"\\|",      "\\\\bar"},
      {"\\%$",   "/\\\\%\\$"},
      {"s/\\~",   "s/\\\\\\~"},
      {"s/\\U",   "s/\\\\U"},
      {"s/\\L",   "s/\\\\L"},
      {"s/\\1",   "s/\\\\1"},
      {"s/\\2",   "s/\\\\2"},
      {"s/\\3",   "s/\\\\3"},
      {"s/\\9",   "s/\\\\9"},
      {NULL, NULL}
   };
   static char *(expr_table[]) = {"!=?", "!~?", "<=?", "<?", "==?", "=~?",
               ">=?", ">?", "is?", "isnot?"};
   int flags;

   d = IObuff;          //assume IObuff is long enough!
   d[0] = ZERO;

   if (STRNICMP(arg, "expr-", 5) == 0) {
      //When the string starting with "expr-" and containing '?' and matches
      //the table, it is taken literally (but ~ is escaped). Otherwise '?'
      //is recognized as a wildcard.
      for (i = (int)ARRAY_LENGTH(expr_table); --i >= 0; ) {
         if (STRCMP(arg + 5, expr_table[i]) == 0) {
            int si = 0, di = 0;

            for (;;) {
                if (arg[si] == '~')
               d[di++] = '\\';
                d[di++] = arg[si];
                if (arg[si] == ZERO)
               break;
                ++si;
            }
            break;
          }
       }
   } else {
      //Recognize a few exceptions to the rule.  Some strings that contain
      //'*'are changed to "star", otherwise '*' is recognized as a wildcard.
      for (i = 0; except_tbl[i][0] != NULL; ++i) {
         if (STRCMP(arg, except_tbl[i][0]) == 0) {
            STRCPY(d, except_tbl[i][1]);
            break;
         }
      }
   }
    
   if (d[0] == ZERO) {//no match in table
      //Replace "\S" with "/\\S", etc.  Otherwise every tag is matched.
      //Also replace "\%^" and "\%(", they match every tag too.
      //Also "\zs", "\z1", etc.
      //Also "\@<", "\@=", "\@<=", etc.
      //And also "\_$" and "\_^".
      if (arg[0] == '\\'
         && ((arg[1] != ZERO && arg[2] == ZERO)
             || (firstOccurrence((CS)"%_z@", arg[1]) != NULL
                           && arg[2] != ZERO))
      ) {
         eeSnprintf(d, IOSIZE, "/\\\\%s", arg + 1);
         //Check for "/\\_$", should be "/\\_\$"
         if (d[3] == '_' && d[4] == '$')
            STRCPY(d + 4, "\\$");
      } else {
         //Replace:
         //"[:...:]" with "\[:...:]"
         //"[++...]" with "\[++...]"
         //"\{" with "\\{"         -- matching "} \}"
         if ((arg[0] == '[' && (arg[1] == ':'
             || (arg[1] == '+' && arg[2] == '+')))
             || (arg[0] == '\\' && arg[1] == '{'))
            *d++ = '\\';

         //If tag starts with "('", skip the "(". Fixes CTRL-] on ('option'.
         if (*arg == '(' && arg[1] == '\'')
            arg++;
         for (s = arg; *s; ++s)  {
            //Replace "|" with "bar" and '"' with "quote" to match the name of
            //the tags for these commands.
            //Replace "*" with ".*" and "?" with "." to match command line completion.
            //Insert a backslash before '~', '$' and '.' to avoid their special meaning.
            if (d - IObuff > IOSIZE - 10)   //getting too long!?
               break;
               
            switch (*s) {
            case '|':   STRCPY(d, "bar");
               d += 3;
               continue;
            case '"':   STRCPY(d, "quote");
               d += 5;
               continue;
            case '*':   *d++ = '.';
               break;
            case '?':   *d++ = '.';
               continue;
            case '$':
            case '.':
            case '~':   *d++ = '\\';
               break;
            }

            //Replace "^x" by "CTRL-X". Don't do this for "^_" to make
            //":help i_^_CTRL-D" work.
            //Insert '-' before and after "CTRL-X" when applicable.
            if (*s < ' ' || (*s == '^' && s[1] && (ASCII_ISALPHA(s[1])
                  || firstOccurrence((CS)"?@[\\]^", s[1]) != NULL))
            ){
               if (d > IObuff && d[-1] != '_' && d[-1] != '\\')
                  *d++ = '_';      //prepend a '_' to make x_CTRL-x
               STRCPY(d, "CTRL-");
               d += 5;
               if (*s < ' ') {
                  *d++ = *s + '@';
                  if (d[-1] == '\\')
                     *d++ = '\\';   //double a backslash
               } else
                  *d++ = *++s;
               if (s[1] != ZERO && s[1] != '_')
                  *d++ = '_';      //append a '_'
               continue;
            } ei (*s == '^')      //"^" or "CTRL-^" or "^_"
               *d++ = '\\';

            //Insert a backslash before a backslash after a slash, for search
            //pattern tags: "/\|" --> "/\\|".
            ei (s[0] == '\\' && s[1] != '\\' && *arg == '/' && s == arg + 1)
               *d++ = '\\';

            //"CTRL-\_" -> "CTRL-\\_" to avoid the special meaning of "\_" in "CTRL-\_CTRL-N"
            if (STRNICMP(s, "CTRL-\\_", 7) == 0) {
               STRCPY(d, "CTRL-\\\\");
               d += 7;
               s += 6;
            }

            *d++ = *s;

            //If tag contains "({" or "([", tag terminates at the "(".
            //This is for help on functions, e.g.: abs({expr}).
            if (*s == '(' && (s[1] == '{' || s[1] =='['))
               break;

            //If tag starts with ', toss everything after a second '. Fixes
            //CTRL-] on 'option'. (would include the trailing '.').
            if (*s == '\'' && s > arg && *arg == '\'')
               break;
            //Also '{' and '}'.
            if (*s == '}' && s > arg && *arg == '{')
               break;
         }
         *d = ZERO;

         if (*IObuff == '`') {
            if (d > IObuff + 2 && d[-1] == '`') {
               //remove the backticks from `command`
               MEMMOVE(IObuff, IObuff + 1, STRLEN(IObuff));
               d[-2] = ZERO;
            } ei (d > IObuff + 3 && d[-2] == '`' && d[-1] == ',') {
               //remove the backticks and comma from `command`,
               MEMMOVE(IObuff, IObuff + 1, STRLEN(IObuff));
               d[-3] = ZERO;
            } ei (d > IObuff + 4 && d[-3] == '`' && d[-2] == '\\' && d[-1] == '.') {
               //remove the backticks and dot from `command`\.
               MEMMOVE(IObuff, IObuff + 1, STRLEN(IObuff));
               d[-4] = ZERO;
            }
         }
      }
   }

   *matches = (ExpandMatch){};
   flags = TAG_HELP | TAG_REGEXP | TAG_NAMES | TAG_VERBOSE | TAG_NO_TAGFUNC;
   if (keep_lang)
      flags |= TAG_KEEP_LANG;
   if (find_tags(IObuff, flags, (int)MAXCOL, NULL, OUT matches) == OK
       && matches->len > 0) {
      //Sort the matches found on the heuristic number that is after the tag name.
      qsort((void *)matches->c, (Unt)matches->len, sizeof(CS), helpCompare);
      //Delete more than TAG_MANY to reduce the size of the listing.
      while (matches->len > TAG_MANY) {
         --matches->len;
         eeglFree(matches->c[matches->len]);
      } 
   }
   return OK;
}

//Cleanup matches for help tags: Remove "@ab" if the top of 'helplang' is "ab" and the language 
//of the first tag matches it.  Otherwise remove "@en" if "en" is the only language.
pub void
cleanup_help_tags(OUT ExpandMatch* matches) {
   int len;
   Byte buf[4];
   buf[3] = ZERO;
   CS p = buf;

   if (p_hlg && (p_hlg[0] != 'e' || p_hlg[1] != 'n')) {
      *p++ = '@';
      *p++ = p_hlg[0];
      *p++ = p_hlg[1];
   }

   for (Unt i = 0; i < matches->len; ++i) {
      len = (int)STRLEN(matches->c[i]) - 3;
      if (len <= 0)
         continue;
      if (STRCMP(matches->c[i] + len, "@en") == 0) {
         //Sorting on priority means the same item in another language may
         //be anywhere. Search all items for a match up to the "@en".
         Unt j;
         for (j = 0; j < matches->len; ++j) {
            if (j != i && (int)STRLEN(matches->c[j]) == len + 3
                  && STRNCMP(matches->c[i], matches->c[j], len + 1) == 0
            )
                break;
         } 
         if (j == matches->len)
            //item only exists with @en, remove it
            matches->c[i][len] = ZERO;
      }
   }

   if (*buf != ZERO) {
      for (Unt i = 0; i < matches->len; ++i) {
         len = (int)STRLEN(matches->c[i]) - 3;
         if (len <= 0)
            continue;
         if (STRCMP(matches->c[i] + len, buf) == 0) {
            //remove the default language
            matches->c[i][len] = ZERO;
         }
      }
   } 
}

//Called when starting to edit a book for a help file.
pub void
prepare_help_buffer(void) {
   curBook->kind = BOOK_HELP;
   optSetByName(S"booktype", optEnum(BOOK_HELP), SET_LOCAL);

   //Always set these options after jumping to a help tag, because the
   //user may have an autocommand that gets in the way.
   //When adding an option here, also update the help file helphelp.txt.

   //Accept all ASCII chars for keywords, except ' ', '*', '"', '|', and
   //latin1 word characters (for translated help files).
   CS p = S"!-~,^*,^|,^\",192-255";
   if (curBook->o.isKeyword && STRCMP(curBook->o.isKeyword, p) != 0) {
      optChangeStringOptionDirect(S"iskeyword", p, OPT_LOCAL, 0);
   }

   curBook->o.shiftWidth = 3;      //tab size is 8
   curPor->o.list = false;   //no list mode

   curBook->o.binary = false;   //reset 'bin' before reading file
   curPor->o.relativeNumber = false;   //no relative line numbers
   curPor->o.foldEnable = false;   //No folding in the help portal
   curPor->o.diff = false;   //No 'diff', no scroll or cursor binding

   bookSetBooklisted(false);
}

//After reading a help file: May cleanup a help book when syntax highlighting is not used.
pub void
searchFixHelpBook(void) {
   CS line;
   int in_example = false;
   int len;

   //Set filetype to "help" if still needed.
   if (STRCMP(curBook->fileType, "help") != 0) {
      ++curBookLock;
      curBook->kind = BOOK_HELP;
      --curBookLock;
   }

   if (!syntax_present(curPor)) {
      for (LineNr lnum = 1; lnum <= curBook->mem.lineCount; ++lnum) {
         line = memGetLine(curBook, lnum, false);
         len = memGetBookLen(curBook, lnum);
         if (in_example && len > 0 && !SPACE_OR_TAB(line[0])) {
            //End of example: non-white or '<' in first column.
            if (line[0] == '<') {
               //blank-out a '<' in the first column
               line = memGetLine(curBook, lnum, true);
               line[0] = ' ';
            }
            in_example = false;
         }
         if (!in_example && len > 0) {
            if (line[len - 1] == '>' && (len == 1 || line[len - 2] == ' ')) {
               //blank-out a '>' in the last column (start of example)
               line = memGetLine(curBook, lnum, true);
               line[len - 1] = ' ';
               in_example = true;
            } ei (line[len - 1] == '~') {
               //blank-out a '~' at the end of line (header marker)
               line = memGetLine(curBook, lnum, true);
               line[len - 1] = ' ';
            }
         }
      }
   }

   //In the "help.txt" and "help.abx" file, add the locally added help
   //files. This uses the very first line in the help file.
   CS fname = fiGetShortFiName(curBook->currFileName);
   if (fnamecmp(fname, "help.txt") == 0
      || (STRNCMP(fname, "help.", 5) == 0
          && ASCII_ISALPHA(fname[5])
          && ASCII_ISALPHA(fname[6])
          && TOLOWER_ASC(fname[7]) == 'x'
          && fname[8] == ZERO)
   ){
      for (LineNr lnum = 1; lnum < curBook->mem.lineCount; ++lnum) {
         line = memGetLine(curBook, lnum, false);
         if (strstr((char *)line, "*local-additions*") == NULL)
            continue;

         //Go through all directories in 'runtimepath', skipping $EEGLRUNTIME.
         ExpandMatch files = {};
         files.a = createArena();
         FILE   *fd;
         CS s;
         CS cp;

         //Find all "doc/ *.help" files in this directory.
         STRCAT(nameBuffG, "*.??[help]");
         if (gen_expand_wildcards(1, &nameBuffG, EW_FILE|EW_SILENT, OUT &files) == OK
             && files.len > 0
         ){
            Unt i2;
            Unt i1;
            Byte   *f1, *f2;
            Byte   *t1, *t2;
            Byte   *e1, *e2;

            for (i1 = 0; i1 < files.len; ++i1) {
               f1 = files.c[i1];
               t1 = fiGetShortFiName(f1);
               e1 = lastOccurrence(t1, '.');
               if (e1 == NULL)
                  continue;
               if (fnamecmp(e1, ".help") != 0 && fnamecmp(e1, fname + 4) != 0) {
                  //Not .help, remove it.
                  EE_CLEAR(files.c[i1]);
                  continue;
               }

               for (i2 = i1 + 1; i2 < files.len; ++i2) {
                  f2 = files.c[i2];
                  if (f2 == NULL)
                     continue;
                  t2 = fiGetShortFiName(f2);
                  e2 = lastOccurrence(t2, '.');
                  if (e2 == NULL)
                     continue;
                  if (e1 - f1 != e2 - f2 || STRNCMP(f1, f2, e1 - f1) != 0)
                     continue;
                  if (fnamecmp(e1, ".txt") == 0 && fnamecmp(e2, fname + 4) == 0)
                      EE_CLEAR(files.c[i1]);
               }
            }
            for (Unt fi = 0; fi < files.len; ++fi) {
               if (files.c[fi] == NULL)
                  continue;
               fd = FOPEN(files.c[fi], "r");
               if (fd) {
                  eeFgets(IObuff, IOSIZE, fd);
                  if (IObuff[0] == '*' && (s = firstOccurrence(IObuff + 1, '*')) != NULL) {
                     int   this_utf = MAYBE;

                     //Change tag definition to a reference and remove <CR>/<NL>.
                     IObuff[0] = '|';
                     *s = '|';
                     while (*s != ZERO) {
                        if (*s == '\r' || *s == '\n')
                            *s = ZERO;
                        //The text is utf-8 when a byte above 127 is found and no
                        //illegal byte sequence is found.
                        if (*s >= 0x80 && this_utf != false) {
                           int   l;

                           this_utf = true;
                           l = utf_ptr2len(s);
                           if (l == 1)
                              this_utf = false;
                           s += l - 1;
                        }
                        ++s;
                     }

                     cp = IObuff;
                     ml_append(lnum, cp, (ColNr)0, false);
                     if (cp != IObuff)
                        eeglFree(cp);
                     ++lnum;
                  }
                  fclose(fd);
               }
            }
         }
         deleteArena(files.a);
      }
   }
}

//":exusage"
pub void
c_exusage(Invocation*) {
   executeCommLine(S"help ex-cmd-index");
}

//":usage"
pub void
c_usage(Invocation*) {
   executeCommLine(S"help normal-index");
}

//Generate tags in one help directory.
private void
generateHelpTagsForDir(
   CS dir,              //doc directory
   CS ext,              //suffix, ".txt", ".itx", ".frx", etc.
   CS tagfname,         //"tags" for English, "tags-fr" for French.
   int add_help_tags,   //add "help-tags" tag
   int ignore_writeerr  //ignore write error
){
   ArrayList   ga;
   CS p1;
   CS p2;
   CS s;
   int i;
   int utf8 = MAYBE;
   int this_utf8;
   int firstline;
   int in_example;
   int len;
   int mix = false;   //detected mixed encodings

   //Find all *.txt files.
   int dirlen = (int)STRLEN(dir);
   STRCPY(nameBuffG, dir);
   STRCAT(nameBuffG, "/**/*");
   STRCAT(nameBuffG, ext);
   
   ExpandMatch files = {};
   files.a = createArena();
   
   int res = gen_expand_wildcards(1, &nameBuffG, EW_FILE|EW_SILENT, OUT &files);
   if (res == FAIL || files.len == 0) {
      if (!gotInterruptG)
          showErrFmtMsg(_(e_no_match_str_1), nameBuffG);
      deleteArena(files.a);
      return;
   }

   //Open the tags file for writing. Do this before scanning through all the files.
   STRCPY(nameBuffG, dir);
   add_pathsep(nameBuffG);
   STRCAT(nameBuffG, tagfname);
   FILE* fd_tags = FOPEN(nameBuffG, "w");
   if (!fd_tags) {
      if (!ignore_writeerr)
         showErrFmtMsg(_(e_cannot_open_str_for_writing_1), nameBuffG);
      deleteArena(files.a);
      return;
   }

   //If using the "++t" argument or generating tags for "docs" add the "help-tags" tag.
   ga_init2(&ga, sizeof(CS), 100);
   if (add_help_tags || fullpathcmp(PREFIX "/share/cim/doc", dir, false, true) == FPC_SAME){
      if (ga_grow(&ga, 1) == FAIL)
         gotInterruptG = true;
      else {
         s = alloc(18 + (unsigned)STRLEN(tagfname));
         SPRINTF(s, "help-tags\t%s\t1\n", tagfname);
         ((Byte **)ga.c)[ga.len] = s;
         ++ga.len;
      }
   }

   //Go over all the files and extract the tags.
   for (Unt fi = 0; fi < files.len && !gotInterruptG; ++fi) {
      FILE* fd = fopen((char *)files.c[fi], "r");
      if (!fd) {
         showErrFmtMsg(_(e_unable_to_open_str_for_reading), files.c[fi]);
         continue;
      }
      CS fname = files.c[fi] + dirlen + 1;

      in_example = false;
      firstline = true;
      while (!eeFgets(IObuff, IOSIZE, fd) && !gotInterruptG) {
         if (firstline) {
            //Detect utf-8 file by a non-ASCII char in the first line.
            this_utf8 = MAYBE;
            for (s = IObuff; *s != ZERO; ++s) {
               if (*s >= 0x80) {
                  this_utf8 = true;
                  int l = utf_ptr2len(s);
                  if (l == 1) {
                     //Illegal UTF-8 byte sequence.
                     this_utf8 = false;
                     break;
                  }
                  s += l - 1;
               }
            } 
            if (this_utf8 == MAYBE)       //only ASCII characters found
               this_utf8 = false;
            if (utf8 == MAYBE)       //first file
               utf8 = this_utf8;
            ei (utf8 != this_utf8) {
               showErrFmtMsg(_(e_mix_of_help_file_encodings_within_language_str), files.c[fi]);
               mix = !gotInterruptG;
               gotInterruptG = true;
            }
            firstline = false;
         }
         if (in_example) {
            //skip over example; a non-white in the first column ends it
            if (firstOccurrence((CS)" \t\n\r", IObuff[0]))
               continue;
            in_example = false;
         }
         p1 = firstOccurrence(IObuff, '*');   //find first '*'
         while (p1 != NULL) {
            //TODO Use eeStrbyte() instead of firstOccurrence() so that when
            //'encoding' is dbcs it still works, don't find '*' in the second byte.
            p2 = eeStrbyte(p1 + 1, '*');    //find second '*'
            if (p2 != NULL && p2 > p1 + 1) { //skip "*" and "**"
               for (s = p1 + 1; s < p2; ++s) {
                  if (*s == ' ' || *s == '\t' || *s == '|')
                     break;
               } 

               //Only accept a *tag* when it consists of valid
               //characters, there is white space before it and is
               //followed by a white character or end-of-line.
               if (s == p2
                   && (p1 == IObuff || p1[-1] == ' ' || p1[-1] == '\t')
                   && (firstOccurrence((CS)" \t\n\r", s[1]) != NULL
                  || s[1] == '\0')
               ) {
                  *p2 = '\0';
                  ++p1;
                  if (ga_grow(&ga, 1) == FAIL) {
                     gotInterruptG = true;
                     break;
                  }
                  s = alloc(p2 - p1 + STRLEN(fname) + 2);
                  ((Byte **)ga.c)[ga.len] = s;
                  ++ga.len;
                  sprintf((char *)s, "%s\t%s", p1, fname);

                  //find next '*'
                  p2 = firstOccurrence(p2 + 1, '*');
               }
            }
            p1 = p2;
         }
         len = (int)STRLEN(IObuff);
         if ((len == 2 && STRCMP(&IObuff[len - 2], ">\n") == 0)
                || (len >= 3 && STRCMP(&IObuff[len - 3], " >\n") == 0))
            in_example = true;
         line_breakcheck();
      }

      fclose(fd);
   }

   deleteArena(files.a);

   if (!gotInterruptG) {
      //Sort the tags.
      if (ga.c)
         sortStrings((Byte **)ga.c, ga.len);

      //Check for duplicates.
      for (i = 1; i < ga.len; ++i) {
         p1 = ((Byte **)ga.c)[i - 1];
         p2 = ((Byte **)ga.c)[i];
         while (*p1 == *p2) {
            if (*p2 == '\t') {
               *p2 = ZERO;
               eeSnprintf(nameBuffG, MAXPATHL,
                  _(e_duplicate_tag_str_in_file_str_str),
                      ((Byte **)ga.c)[i], dir, p2 + 1);
               emsg(nameBuffG);
               *p2 = '\t';
               break;
            }
            ++p1;
            ++p2;
         }
      }

      if (utf8 == true)
          fprintf(fd_tags, "!_TAG_FILE_ENCODING\tutf-8\t//\n");

      //Write the tags into the file.
      for (i = 0; i < ga.len; ++i) {
         s = ((Byte **)ga.c)[i];
         if (STRNCMP(s, "help-tags\t", 10) == 0)
            //help-tags entry was added in formatted form
            fputs((char *)s, fd_tags);
         else {
            fprintf(fd_tags, "%s\t/*", s);
            for (p1 = s; *p1 != '\t'; ++p1) {
                //insert backslash before '\\' and '/'
                if (*p1 == '\\' || *p1 == '/')
               putc('\\', fd_tags);
                putc(*p1, fd_tags);
            }
            fprintf(fd_tags, "*\n");
         }
      }
   }
   if (mix)
      gotInterruptG = false;    //continue with other languages

   for (i = 0; i < ga.len; ++i)
      eeglFree(((Byte **)ga.c)[i]);
   ga_clear(&ga);
   fclose(fd_tags);       //there is no check for an error...
}

//Generate tags in one help directory, taking care of translations.
private void
do_helptags(CS dirname, int add_help_tags, int ignore_writeerr) {
   int      len;
   int      j;
   ArrayList   ga;
   Byte lang[2];
   Byte ext[5];
   Byte fname[8];
   ExpandMatch files = {};

   //Get a list of all files in the help directory and in subdirectories.
   STRCPY(nameBuffG, dirname);
   add_pathsep(nameBuffG);
   STRCAT(nameBuffG, "**");
   if (gen_expand_wildcards(1, &nameBuffG, EW_FILE|EW_SILENT, OUT &files) == FAIL
       || files.len == 0
   ) {
      showErrFmtMsg(_(e_no_match_str_1), nameBuffG);
      return;
   }

   //Go over all files in the directory to find out what languages are present.
   ga_init2(&ga, 1, 10);
   for (Unt i = 0; i < files.len; ++i) {
      len = (int)STRLEN(files.c[i]);
      if (len <= 4)
          continue;

      if (caseInsensitiveCompare(files.c[i] + len - 4, ".txt") == 0) {
         //".txt" -> language "en"
         lang[0] = 'e';
         lang[1] = 'n';
      } else
         continue;

      //Did we find this language already?
      for (j = 0; j < ga.len; j += 2) {
         if (STRNCMP(lang, ((CS)ga.c) + j, 2) == 0)
            break;
      } 
      if (j == ga.len) {
         //New language, add it.
         if (ga_grow(&ga, 2) == FAIL)
            break;
         ((CS)ga.c)[ga.len++] = lang[0];
         ((CS)ga.c)[ga.len++] = lang[1];
      }
   }

   //Loop over the found languages to generate a tags file for each one.
   for (j = 0; j < ga.len; j += 2) {
      STRCPY(fname, "tags-xx");
      fname[5] = ((CS)ga.c)[j];
      fname[6] = ((CS)ga.c)[j + 1];
      if (fname[5] == 'e' && fname[6] == 'n') {
          //English is an exception: use ".txt" and "tags".
          fname[4] = ZERO;
          STRCPY(ext, ".txt");
      } else {
          //Language "ab" uses ".abx" and "tags-ab".
          STRCPY(ext, ".xxx");
          ext[1] = fname[5];
          ext[2] = fname[6];
      }
      generateHelpTagsForDir(dirname, ext, fname, add_help_tags, ignore_writeerr);
   }

   ga_clear(&ga);
   deleteArena(files.a); 
}

private void
helptagsCb(CS fname, void* cookie) {
   do_helptags(fname, *(int *)cookie, true);
}

//":helptags"
pub void
c_helptags(Invocation* invo) {
   Expand expand;
   CS dirname;
   Boole add_help_tags = false;

   //Check for ":helptags ++t {dir}".
   if (STRNCMP(invo->arg, "++t", 3) == 0 && SPACE_OR_TAB(invo->arg[3])) {
      add_help_tags = true;
      invo->arg = skipwhite(invo->arg + 3);
   }

   if (STRCMP(invo->arg, "ALL") == 0) {
      doInPath(PREFIX "/share/doc/", S"", S"eegl", DIP_ALL + DIP_DIR, helptagsCb, &add_help_tags);
   } else {
      expandInit(&expand);
      expand.context = EXPAND_DIRECTORIES;
      dirname = expandWildcard(
            OUT &expand, invo->arg, NULL, WILD_LIST_NOTFOUND|WILD_SILENT, WILD_EXPAND_FREE
      );
      if (dirname == NULL || !mch_isdir(dirname))
         showErrFmtMsg(_(e_not_a_directory_str), invo->arg);
      else
         do_helptags(dirname, add_help_tags, false);
      eeglFree(dirname);
   }
}

//}}}
