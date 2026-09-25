//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## book.c: functions for dealing with the Book structure (i.e. open files)

//The book list is a double linked list of all books.
//Each book can be in one of these states:
//never loaded: BF_NEVERLOADED is set, only the file name is valid
//  not loaded: mem.mfile == NULL, no memfile allocated
//      hidden: countPortals == 0, loaded but not displayed in a portal
//      normal: loaded and displayed in a portal
//
//Instead of storing file names all over the place, each file name is
//stored in the book list. It can be referenced by a number.
//
//The current implementation remembers all file names ever used.

#include "eegl.h"
#include "h/book.h"
#include "h/data.types.h"
#include "h/data.h"
#include "h/channel.types.h"
#include "h/channel.h"
#include "h/diff.h"
#include "h/do.h"
#include "h/draw.types.h"
#include "h/draw.h"
#include "h/eval.h"
#include "h/hilite.types.h"
#include "h/hilite.h"
#include "h/input.types.h"
#include "h/input.h"
#include "h/message.types.h"
#include "h/message.h"
#include "h/fileio.h"
#include "h/location.types.h"
#include "h/location.h"
#include "h/motor.types.h"
#include "h/motor.h"
#include "h/option.h"
#include "h/portal.h"
#include "h/regexp.h"
#include "h/search.h"
#include "h/strings.h"
#include "h/script.h"
#include "h/term.h"
#include "h/ui.h"
#include "h/wheel.types.h"
#include "h/wheel.h"

#include <fcntl.h>      // Definition of AT_* constants for utimensat()
#include <sys/stat.h> // for stat,  utimensat() (modification time changin')
#include <sys/sysinfo.h>

//{{{types

//MfHashTable is a chained hashtable with BlockId key and arbitrary structures as items. This is 
//an intrusive data structure: we require that items begin with MfHashItem which contains the key 
//and linked list pointers.  List of items in each bucket is doubly-linked.
struct MfHashItem {
   MfHashItem* next;
   MfHashItem* prev;
   BlockId key;
};

//when a block with a negative number is flushed to the file, it gets
//a positive number. Because the reference to the block is still the negative
//number, we remember the translation to the new positive number in the
//double linked trans lists. The structure is the same as the hash lists.
typedef struct {
   MfHashItem nt_hashitem;      // header for hash table and key
#define nt_old_bnum nt_hashitem.key   // old, negative, number
   BlockId   nt_new_bnum;      // new, positive, number
} NrTranslation;

#define B0_FNAME_SIZE_ORG     900   // what it was in older versions
#define B0_FNAME_SIZE_NOCRYPT 898   // 2 bytes used for other things
#define B0_FNAME_SIZE_CRYPT   890   // 10 bytes used for other things
#define B0_UNAME_SIZE          40
#define B0_HNAME_SIZE          40

//Block 0 holds all info about the swap file.
//
//NOTE: DEFINITION OF BLOCK 0 SHOULD NOT CHANGE! It would make all existing swap files unusable!
//
//If size of block0 changes anyway, adjust MIN_SWAP_PAGE_SIZE in eegl.h!!
//
//This block is built up of single bytes, to make it portable across
//different machines. b0_magic_* is used to check the byte order and size of
//variables, because the rest of the swap file is not portable.
typedef struct {
   Byte   b0_id[2];   // id for block 0: BLOCK0_ID0 and BLOCK0_ID1,
            // BLOCK0_ID1_C0, BLOCK0_ID1_C1, etc.
   Byte   b0_version[10];   // Eegl version string
   Byte   b0_page_size[4];// number of bytes per page
   Byte   b0_mtime[4];   // last modification time of file
   Byte   b0_ino[4];   // inode of b0_fname
   Byte   b0_pid[4];   // process id of creator (or 0)
   Byte   b0_uname[B0_UNAME_SIZE]; // name of user (uid if no name)
   Byte   b0_hname[B0_HNAME_SIZE]; // host name (if it has a name)
   Byte   b0_fname[B0_FNAME_SIZE_ORG]; // name of file being edited
   Long   b0_magic_long;   // check for byte order of long
   int    b0_magic_int;   // check for byte order of int
   Short  b0_magic_short;   // check for byte order of short
   Byte   b0_magic_char;   // check for last char
} Block0;

// argument for updateBlock0()
typedef enum {
   UB_FNAME = 0, // update timestamp and filename
   UB_SAME_DIR,  // update the B0_SAME_DIR flag
   UB_CRYPT      // update crypt key
} UpdBlock0;

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
pub typedef struct mf_hashtab_S {
   Ulong mask; // mask used for hash value (nr of items in array is "mht_mask" + 1)
   Ulong mht_count;       // nr of items inserted into hashtable
   MfHashItem** mht_buckets;  // points to mht_small_buckets or dynamically allocated array
   MfHashItem* mht_small_buckets[MHT_INIT_SIZE];   // initial buckets
   Byte mht_fixed;       // non-zero value forbids growth
} MfHashTable;

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

pub typedef enum {
   MF_DIRTY_NO = 0,      // no dirty blocks
   MF_DIRTY_YES,      // there are dirty blocks
   MF_DIRTY_YES_NOSYNC,   // there are dirty blocks, do not sync yet
} MfDirty;

struct MemChunkSize {
   int mlcs_numlines;
   Long mlcs_totalsize;
};

pub
struct MemFile {
   CS fullFName;      // name of the file
   CS fName;          // idem, full path
   int fd;         // file descriptor
   Unt mf_flags;      // flags used when opening this memfile
   int mf_reopen;      // mf_fd was closed, retry opening
   BlockHeader* freeFirst;      // first block_hdr in free list
   BlockHeader* usedFirst;      // mru block_hdr in used list
   BlockHeader* usedLast;      // lru block_hdr in used list
   Unt mf_used_count;      // number of pages in used list
   Unt usedCountMax;   // maximum number of pages in memory
   MfHashTable mf_hash;      // hash lists
   MfHashTable mf_trans;      // trans lists
   BlockId mf_blocknr_max;      // highest positive block number + 1
   BlockId mf_blocknr_min;      // lowest negative block number - 1
   BlockId mf_neg_count;      // number of negative blocks numbers
   BlockId pagesInFile;   // number of pages in the file
   Unt pageSize;      // number of bytes in a page
   MfDirty mf_dirty;
   Book* book;      // book this memfile is for
};

typedef enum {
   SEA_CHOICE_NONE = 0,
   SEA_CHOICE_READONLY = 1,
   SEA_CHOICE_EDIT = 2,
   SEA_CHOICE_RECOVER = 3,
   SEA_CHOICE_DELETE = 4,
   SEA_CHOICE_QUIT = 5,
   SEA_CHOICE_ABORT = 6
} SeaChoice;


typedef struct {
   Portal* curPorSave;
   AutocommSave autocommSave;
   Boole usingAco;
   int visualActiveSaved;
} ChangeOtherBook;

// Structure to pass arguments from bookWrite() to writeBytes().
typedef struct {
   CS bw_buf;   // buffer with data to be written
   int fd;      // file descriptor
   int bw_len;      // length of data
   Book* tgt;   // book being written
   int bw_first;   // first write call
   LineNr bw_start_lnum;   // line number at start of book
} BwInfo;

// State used by the :all command to open all the files in the argument list in separate portals
typedef struct {
   EeArgList* alist;      // argument list to be used
   int   had_tab;
   int   keep_tabs;
   int   forceit;

   int      use_firstPor;   // use first portal for arglist
   Arr(Byte) opened;   // Array of weight for which args are open:
           //  0: not opened
           //  1: opened in other tab
           //  2: opened in curtab
           //  3: opened in curtab and curPor
   int opened_len;   // length of opened[]
   Portal* new_curPor;
   Tab* new_curtab;
} ArgAllState;

typedef struct {
   CS tyName;
   int      id;
   NULLABLE CS text; // if non-empty, the text to display above or before the line
   int      textPaddingLeft;
   int      textFlags;
   LineNr   startLnum;
   LineNr   endLnum;
   ColNr      startCol;
   ColNr      endCol;
} Prop;

// Struct used to return two values from adjust().
typedef struct {
   int dirty;      // if the property was changed
   int mayDrop;   // whether after this change, the prop may be removed
} AdjustRes;


//}}}
//{{{@@forward declarations
private int ml_check_b0_id(Block0* b0p);
private void updateBlock0(Book *book, UpdBlock0 what);
private void set_b0_fname(Block0 *b0p, Book *book);
private void set_b0_dir_flag(Block0* b0p, Book* book);
private int swapfile_process_running(Block0 *b0p, CS swap_fname);
private int swapfile_unchanged(CS fname);
private void addTextPropsForAppend(
   Book* book,
   LineNr   lnum,
   OUT CS* lineContent,
   OUT int* len,
   OUT Byte** tofree
);
private int insertLineText(
   Book* book,
   LineNr lnum,      // append after this line (can be 0)
   CS newContentArg, // text of the new line
   ColNr lenArgWithZeroChar,   // length of line, including ZERO, or 0
   Unt flags      // ML_APPEND_ flags
);
private int appendFlush(
   Book   *book,
   LineNr   lnum,      // append after this line (can be 0)
   Arr(Byte) newContent,   // text of the new line
   ColNr   len,      // length of line, including ZERO, or 0
   int      flags      // ML_APPEND_ flags
);
private void adjustTextPropsForDeletion(
   Book* book,
   LineNr lnum,
   CS del_props,
   int del_props_len,
   int above
);
private int deleteLine(Book* book, LineNr lnum, int flags);
private void flushLine(Book *book);
private BlockHeader * newDataBlock(MemFile *mfp, int negative, int pageCount);
private BlockHeader * ml_new_ptr(MemFile *mfp);
private BlockHeader * ml_find_line(Book *book, LineNr lnum, int action);
private int ml_add_stack(Book* book);
private void fixBlockStack(Book* book, int count);
private void attention_message(Book* book, CS swapName);
private SeaChoice  do_swapexists(Book* book, CS fname);
private CS findSwapName(Book* book, CS old_fname);
private int b0_magic_wrong(Block0* b0p);
private int compareFnameWithInode(CS fname_c, CS nameFromSwap, long ino_block0);
private void longToChar(long n, CS s);
private long charToLong(CS s);
private void updateChunk(Book* book, LineNr line, Long len, int updtype);
private Tyme swapfile_info(CS fname);
private void mf_ins_hash(MemFile* mfp, BlockHeader* hp);
private void mf_rem_hash(MemFile* mfp, BlockHeader* hp);
private BlockHeader * mf_find_hash(MemFile* mfp, BlockId nr);
private void mf_ins_used(MemFile* mfp, BlockHeader* hp);
private void mf_rem_used(MemFile* mfp, BlockHeader* hp);
private BlockHeader * mf_release(MemFile* mfp, int page_count);
private BlockHeader * mf_alloc_bhdr(MemFile* mfp, int page_count);
private void mf_free_bhdr(BlockHeader* hp);
private void mf_ins_free(MemFile* mfp, BlockHeader* hp);
private BlockHeader * mf_rem_free(MemFile* mfp);
private int mf_read(MemFile* mfp, BlockHeader* hp);
private int mf_write(MemFile* mfp, BlockHeader* hp);
private int mf_write_block(MemFile* mfp, BlockHeader* hp, FileOffset, unsigned size);
private int mf_trans_add(MemFile* mfp, BlockHeader* hp);
private void mf_do_open(MemFile* mfp, CS fname, Unt flags);
private void mf_hash_init(MfHashTable*  mht);
private void mf_hash_free(MfHashTable *mht);
private void mf_hash_free_all(MfHashTable* mht);
private MfHashItem * mf_hash_find(MfHashTable *mht, BlockId key);
private void mf_hash_add_item(MfHashTable* mht, MfHashItem* mhi);
private void mf_hash_rem_item(MfHashTable *mht, MfHashItem *mhi);
private int mf_hash_grow(MfHashTable *mht);
private void findPortalIntoCurBook(void);
private void prepareChangeInOtherBook(ChangeOtherBook *cob, Book* book);
private void restoreChangeInOtherBook(ChangeOtherBook* cob);
private void updateLinesFromVars(
   Book* book,
   LineNr lnum_arg,
   Boole append,
   Arr(Var) lines,
   Var* returnVar
);
private void setOrAppendLines(Arr(Var) argvars, OUT Var* returnVar, Boole append);
private void bufPortalCommon(Var* argvars, Var* returnVar, int get_nr);
private LineNr findLnum(Book* book);
private Bag * getBookInfo(Book* book);
private void getLinesIntoVar(
   Book   *book,
   LineNr   start,
   LineNr   end,
   int      retlist,
   OUT Var   *returnVar
);
private void getBookLineIntoVar(Var* argvars, Var* returnVar, int retlist);
private int initCharTable(Book* book);
private int parseAnIsOption(CS var, Book* book, Boole only_check);
private int win_nolbr_chartabsize(CharTableSize* cts, int* headp);
private int                                                                     inPortalBorder(Portal *po, ColNr vcol);
private int calc_percentage(long part, long whole);
private int readBook(
   int read_stdin,       // read file from stdin, otherwise fifo
   Invocation* invo,          // for forced 'ff' or NULL
   Unt      flags          // extra flags for readfile()
);
private void addBookToHashtable(Book* book);
private void removeBookFromHashtable(Book* book);
private Boole canUnloadBook(Book* book);
private void freeBook(Book* book);
private void init_changedtick(Book* book);
private void clearPortInfo(Book* book);
private void freeAttachedData(Book* book, int free_options);
private int emptyCurBook(int portCloseOthers, Boole forceit, Unt action);
private void enterBook(Book* book);
private void no_write_message_buf(Book* book);
private Boole isCurBookReusable(void);
private void getLastKnownLineNumber(void);
private Boole sameFileInBook(Book* book, CS fullFName, FileStat* stp);
private Book* booklistFindName_stat(CS fullFName, FileStat* stp);
private CS checkFilenameMatch(RegMatch* rmp, Book* book);
private CS fname_match(RegMatch* rmp, CS name);
private int wininfo_other_tab_diff(PortInfo* poInfo);
private PortInfo* find_wininfo(Book* book, int need_options, Boole skipDiffBook);
private int areSameInode(Book* book, FileStat* stp);
private int append_arg_number(Portal* po, CS buf, Unt buflen);
private int bt_nofileread(Book* book);
private void listInColumns(Arr(CS) items, int size, int current, Boole useHilite);
private void deleteMsg(CS name);
private int writeBytes(BwInfo* ip);
private int check_mtime(Book* book, FileStat *st);
private void updateFileTime(
   CS fname,
   Tyme  atime,      // access time
   Tyme  mtime       // modification time
);
private CS determineBackupFilename(CS fname, CS dname);
private int check_arglist_locked(void);
private void alist_set(
   EeArgList* al,
   Boole useCurBook,
   Arr(int) fnum_list,
   Unt fnum_len,
   OUT ExpandMatch* files
);
private CS do_one_arg(CS str);
private int get_arglist(ArrayList *gap, CS str, Boole escaped);
private void alist_check_arg_idx(void);
private void alist_add_list(
   ExpandMatch files,
   int after,       // where to add: 0 = before first one
   Boole will_edit  // will edit adding argument
);
private void arglist_del_files(ArrayList *alist_ga);
private int do_arglist(
   CS str,
   int what,
   int after,   // 0 means before first one
   Boole will_edit   // will edit added argument
);
private void argAllCloseUnusedPortals(ArgAllState *aall);
private void openPortalsIntoFiles(ArgAllState *aall, int count);
private void openAllArgs(
    int   count,
    int   forceit,      // hide books in current portals
    int keep_tabs      // keep current tabs, for ":tab drop file"
);
private void get_arglist_as_returnVar(ArgFileEntry *arglist, Unt argcount, OUT Var* returnVar);
private EeSetItem * findPropTypeHash(Text name, Book* book);
private PropType * findPropTypeByName(Text name, Book* book);
private PropType * lookup_prop_type(Text name, Book* book);
private Unt getBookNrFromArg(Var *arg, Book** book);
private int addProp(OUT Book* book, Prop prop);
private int get_textprop_id(Book* book);
private int text_prop_order(int flags);
private int text_prop_compare(const void *s0, const void *s1);
private void set_text_props(LineNr lnum, CS props, int len);
private int compare_pt(void const * s0, void const* s1);
private PropType* find_type_by_id(EeSet* ht, PropType*** array, int id);
private void prop_fill_dict(Bag* dict, TextProp* prop, Book* book);
private int text_prop_type_valid(Book* book, TextProp *prop);
private int prop_type_or_id_in_list(int *types_or_ids, int len, int type_or_id);
private void get_props_in_line(
   Book* book,
   LineNr lnum,
   int* prop_types,
   int prop_types_len,
   int* prop_ids,
   int prop_ids_len,
   List* retlist,
   int add_lnum
);
private int * get_prop_types_from_names(List *l, Book* book, OUT int *num_types);
private int* get_prop_ids_from_list(List *l, OUT int* countIds);
private void prop_type_set(Var *argvars, int add);
private void list_types(EeSet *ht, List *l);
private void clear_ht_prop_types(EeSet *ht);
private void clearPropTypes(Book* book);
private AdjustRes adjust(
   TextProp  *prop,
   ColNr       col,
   int       added,
   int       flags
);
#define add(a, b) _Generic((a),\
   LBufMatch*: add_LBufMatch\
   )(a, b)
//}}}
//{{{memline

// When searching for a specific line, we remember what blocks in the tree
// are the branches leading to that block. This is stored in ml_stack.  Each
// entry is a pointer to info in a block (may be data block or pointer block)
struct InfoPtr {
   BlockId   ip_bnum;   // block number
   LineNr   ip_low;      // lowest lnum in this block
   LineNr   ip_high;   // highest lnum in this block
   int      ip_index;   // index for block with current lnum
};   // block/index pair

// flags for mf_sync()
#define MFS_ALL    1   //also sync blocks with negative numbers
#define MFS_STOP   2   //stop syncing when a character is available
#define MFS_FLUSH  4   //flushed file to disk
#define MFS_ZERO   8   //only write block 0

// for debugging
// #define CHECK(c, s)   do { if (c) emsg((s)); } while (0)
#define CHECK(c, s)   do { /**/ } while (0)

//memline.c: Contains the functions for appending, deleting and changing the text lines. The 
//memfile functions are used to store the information in blocks of memory, backed up by a file. 
//The structure of the information is a tree.  The root of the tree is a pointer block. The leaves
//of the tree are data blocks. In between may be several layers of pointer blocks, forming 
//branches.
//
//Three types of blocks are used:
//- Block nr 0 contains information for recovery. It's the Block0 struct below.
//- Pointer blocks contain list of pointers to other blocks.
//- Data blocks contain the actual text.
//
//Block 1 is the first pointer block. It's the root of the tree. Other pointer blocks are branches.
//
//If a line is too big to fit in a single page, the block containing that line is made big enough
//to hold the line. It may span several pages. Otherwise all blocks are one page.
//
//A data block that was filled when starting to edit a file and was not changed since then, can 
//have a negative block number. This means that it has not yet been assigned a place in the 
//file. When recovering, the lines in this data block can be read from the original file. When the
//block is changed (lines appended/deleted/changed) or when it is flushed it gets a positive 
//number. Use mf_trans_del() to get the new number, before calling mf_get().


// Flag that is set when switching off 'swapfile'. It means that all blocks
// are to be loaded into memory.
private int dontReleaseBlocksS = false;

typedef struct PointerBlock   PointerBlock; // contents of a pointer block
typedef struct DataBlock   DataBlock;    // contents of a data block
typedef struct PtrEntry   PtrEntry;         // block/line-count pair

#define DATA_ID          (('d' << 8) + 'a')   // data block id
#define PTR_ID          (('p' << 8) + 't')   // pointer block id
#define BLOCK0_ID0     'b'          // block 0 id 0
#define BLOCK0_ID1     '0'          // block 0 id 1
#define BLOCK0_ID1_C0  'c'          // block 0 id 1 'cm' 0
#define BLOCK0_ID1_C1  'C'          // block 0 id 1 'cm' 1
#define BLOCK0_ID1_C2  'd'          // block 0 id 1 'cm' 2
//BLOCK0_ID1_C3 and BLOCK0_ID1_C4 are for libsodium encryption. However, for
//these the swapfile is disabled, thus they will not be used. Added for consistency anyway.
#define BLOCK0_ID1_C3  'S'          // block 0 id 1 'cm' 3
#define BLOCK0_ID1_C4  's'          // block 0 id 1 'cm' 4


//pointer to a block, used in a pointer block
struct PtrEntry {
   BlockId   blockId;   // block number
   LineNr   lineCount;   // number of lines in this branch
   LineNr   oldLnum;   // lnum for this block (for recovery)
   int      pageCount;   // number of pages in block blockId
};

// A pointer block contains a list of branches in the tree.
struct PointerBlock {
   Short id;             // ID for pointer block: PTR_ID
   Short pointerCount;   // number of pointers in this block
   Short pointerCountMax;// maximum value for pointerCount
   PtrEntry c[1];          // list of pointers to blocks (actually longer) padded by empty space 
                           // until end of page
};

// Value for pointerCountMax.
#define pointerCountMax(mfp) (Short)(((mfp)->pageSize - offsetof(PointerBlock, c)) / sizeof(PtrEntry))

// A data block is a leaf in the tree.
//
// The text of the lines is at the end of the block. The text of the first line
// in the block is put at the end, the text of the second line in front of it,
// etc. Thus the order of the lines is the opposite of the line number.
//
//    [id...countLines|...free...[line2 contents\0][line1 \0][line0 \0]]
//    ^ DataBlock     ^c         ^startByte                             ^ endByte
struct DataBlock {
   Short   id;      // ID for data block: DATA_ID
   unsigned   freeSpace;   // free space available
   unsigned   startByte;   // byte where text starts
   unsigned   endByte;   // byte just after data block
   LineNr   countLines;   // number of lines in this block
   unsigned   c[1];   // index for start of line (flex array) followed by empty space up to 
        // startByte, then by the text in the lines until end of memory page
};

//The low bits of c hold the actual index. The topmost bit is used for the global command to be 
//able to mark a line. This method is not clean, but otherwise there would be at least one extra
//byte used for each line. The mark has to be in this place to keep it with the correct line when 
//other lines are inserted or deleted.
#define DB_MARKED   ((unsigned)1 << ((sizeof(unsigned) * 8) - 1))
#define c_MASK   (~DB_MARKED)

#define INDEX_SIZE  (sizeof(unsigned))       // size of one c entry
#define HEADER_SIZE (offsetof(DataBlock, c))  // size of data block header

// Restrict the numbers to 32 bits, otherwise most compilers will complain.
// This won't detect a 64 bit machine that only swaps a byte in the top 32
// bits, but that is crazy anyway.
#define B0_MAGIC_LONG   0x30313233L
#define B0_MAGIC_INT   0x20212223L
#define B0_MAGIC_SHORT   0x10111213L
#define B0_MAGIC_CHAR   0x55

//Minimal size for block 0 of a swap file.
//NOTE: This depends on size of struct block0! It's not done with a sizeof(),
//because struct block0 is defined in memline.c (Sorry).
//The maximal block size is arbitrary.
#define MIN_SWAP_PAGE_SIZE 1048
#define MAX_SWAP_PAGE_SIZE 50000

// Note: b0_dirty and b0_flags are put at the end of the file name.
#define B0_DIRTY   0x55
#define b0_dirty   b0_fname[B0_FNAME_SIZE_ORG - 1]

#define b0_flags   b0_fname[B0_FNAME_SIZE_ORG - 2]

// Swap file is in directory of edited file. Used to find the file from different mount points
#define B0_SAME_DIR   4

#define STACK_INCR   5   // nr of entries added to ml_stack at a time

// The line number where the first mark may be is remembered. If it is 0 there are no marks at all.
// (always used for the current book only, no book change possible while executing a global 
// command).
private LineNr   lowest_marked = 0;

// arguments for ml_find_line()
#define ML_DELETE    0x11       // delete line
#define ML_INSERT    0x12       // insert line
#define ML_FIND      0x13       // just find the line
#define ML_FLUSH     0x02       // flush locked block
#define ML_SIMPLE(x) ((x) & 0x10)  // DEL, INS or FIND

//Get user name from machine-specific function. Return the user name in "buf[len]".
//Return OK or FAIL.
pub int
get_user_name(CS builder, int len) {
   if (!username) {
      if (mch_get_uname(getuid(), builder, len) == FAIL)
         return FAIL;
      username = copyStr(builder);
   } else
      copySubstrToAllocation(builder, (Text){username, len - 1});
   return OK;
}


//Open a new memline for "book". Return FAIL for failure, OK otherwise.
pub int
ml_open(Book *book) {
   MemFile   *mfp;
   BlockHeader* hdr = NULL;
   Block0   *b0p;
   PointerBlock   *pp;
   DataBlock* block;

   // init fields in memline struct
   book->mem.ml_stack_size = 0; // no stack yet
   book->mem.ml_stack = NULL;   // no stack yet
   book->mem.ml_stack_top = 0;   // nothing in the stack
   book->mem.locked = NULL;   // no cached block
   book->mem.ml_line_lnum = 0;   // no cached line
   book->mem.ml_chunksize = NULL;
   book->mem.ml_usedchunks = 0;

   if (commModifierG.cmod_flags & CMOD_NOSWAPFILE)
      book->o.swapFile = false;

   //When 'updatecount' is non-zero swap file may be opened later.
   if (swapEnabledG && book->o.swapFile)
      book->maySwap = true;
   else
      book->maySwap = false;

   // Open the memfile.  No swap file is created yet.
   mfp = mf_open(NULL, 0);
   if (mfp == NULL)
      goto error;

   book->mem.mfile = mfp;
   mfp->book = book;
   book->mem.flags = ML_EMPTY;
   book->mem.lineCount = 1;

   // fill Block0 struct and write page 0
   if ((hdr = mf_new(mfp, false, 1)) == NULL)
      goto error;
   if (hdr->bh_bnum != 0) {
      internalErrMsg(e_didnt_get_block_nr_zero);
      goto error;
   }
   b0p = (Block0 *)(hdr->bh_data);

   b0p->b0_id[0] = BLOCK0_ID0;
   b0p->b0_id[1] = BLOCK0_ID1;
   b0p->b0_magic_long = (long)B0_MAGIC_LONG;
   b0p->b0_magic_int = (int)B0_MAGIC_INT;
   b0p->b0_magic_short = (short)B0_MAGIC_SHORT;
   b0p->b0_magic_char = B0_MAGIC_CHAR;
   MEMMOVE(b0p->b0_version, "EEGL ", 4);
   STRNCPY(b0p->b0_version + 4, mainProgramVersion(), 6);
   longToChar((long)mfp->pageSize, b0p->b0_page_size);

   b0p->b0_dirty = book->wasModified ? B0_DIRTY : 0;
   b0p->b0_flags = 0;
   set_b0_fname(b0p, book);
   (void)get_user_name(b0p->b0_uname, B0_UNAME_SIZE);
   b0p->b0_uname[B0_UNAME_SIZE - 1] = ZERO;
   mch_get_host_name(b0p->b0_hname, B0_HNAME_SIZE);
   b0p->b0_hname[B0_HNAME_SIZE - 1] = ZERO;
   longToChar(mch_get_pid(), b0p->b0_pid);

   //Always sync block number 0 to disk, so we can check the file name in
   //the swap file in findSwapName(). Don't do this for a help files or a spell book though.
   //Only works when there's a swapfile, otherwise it's done when the file is created.
   mf_put(mfp, hdr, true, false);
   if (book->kind != BOOK_HELP)
      (void)mf_sync(mfp, 0);

   //Fill in root pointer block and write page 1.
   if ((hdr = ml_new_ptr(mfp)) == NULL)
      goto error;
   if (hdr->bh_bnum != 1) {
      internalErrMsg(e_didnt_get_block_nr_one);
      goto error;
   }
   pp = (PointerBlock *)(hdr->bh_data);
   pp->pointerCount = 1;
   pp->c[0].blockId = 2;
   pp->c[0].pageCount = 1;
   pp->c[0].oldLnum = 1;
   pp->c[0].lineCount = 1;    // line count after insertion
   mf_put(mfp, hdr, true, false);

   //Allocate first data block and create an empty line 1.
   if ((hdr = newDataBlock(mfp, false, 1)) == NULL)
      goto error;
   if (hdr->bh_bnum != 2) {
      internalErrMsg(e_didnt_get_block_nr_two);
      goto error;
   }

   block = (DataBlock*)(hdr->bh_data);
   block->c[0] = --block->startByte;   // at end of block
   block->freeSpace -= 1 + INDEX_SIZE;
   block->countLines = 1;
   *((CS)block + block->startByte) = ZERO;   // empty line

   return OK;

error:
   if (mfp) {
      if (hdr)
         mf_put(mfp, hdr, false, false);
      mf_close(mfp, true);       // will also free(mfp->fName)
   }
   book->mem.mfile = NULL;
   return FAIL;
}

//Prepare encryption for "book" for the current key and method.
//ml_setname() is called when the file name of "book" has been changed. It may rename the swap file.
pub void
ml_setname(Book* book) {
   int success = false;
   MemFile* mfp = book->mem.mfile;
   if (mfp->fd < 0)   {    // there is no swap file yet
      //When 'updatecount' is 0 and 'noswapfile' there is no swap file.
      //For help files we will make one now.
      if (swapEnabledG && (commModifierG.cmod_flags & CMOD_NOSWAPFILE) == 0)
         memOpenSwapFile(book);       // create a swap file
      return;
   }

   CS swapName = findSwapName(book, mfp->fName); // alloc's fname
   
   //If swap name is open, need to close it before renaming
   if (mfp->fd >= 0) {
      close(mfp->fd);
      mfp->fd = -1;
   }

   //try to rename the swap file
   if (eeRename(mfp->fName, swapName) == 0) {
      success = true;
      eeglFree(mfp->fName);
      mfp->fName = swapName;
      eeglFree(mfp->fullFName);
      mf_set_ffname(mfp);
      updateBlock0(book, UB_SAME_DIR);
   }
   eeglFree(swapName);       // this fname didn't work, try another

   if (mfp->fd == -1) {    // need to (re)open the swap file
      mfp->fd = open((char *)mfp->fName, O_RDWR | O_EXTRA, 0);
      if (mfp->fd < 0) {
         // could not (re)open the swap file, what can we do????
         emsg(_(e_oops_lost_the_swap_file));
         return;
      }
      int fdflags = fcntl(mfp->fd, F_GETFD);
      if (fdflags >= 0 && (fdflags & FD_CLOEXEC) == 0)
         (void)fcntl(mfp->fd, F_SETFD, fdflags | FD_CLOEXEC);
   }
   if (!success)
      emsg(_(e_could_not_rename_swap_file));
}

//Open a file for the memfile for all books that are not readonly or have been modified.
//Used when 'updatecount' changes from zero to non-zero.
pub void
memOpenSwapFiles(void) {
   Book* book;
   FOR_ALL_BOOKS(book) {
      if (book->o.modifiable || book->wasModified)
         memOpenSwapFile(book);
   } 
}

//Open a swap file for an existing memfile, if there is no swap file yet.
//If we are unable to find a file name, fName will be NULL
//and the memfile will be in memory only (no recovery possible).
pub void
memOpenSwapFile(Book* book) {
   MemFile* mfp = book->mem.mfile;
   if (!mfp || mfp->fd >= 0 || !book->o.swapFile 
         || (commModifierG.cmod_flags & CMOD_NOSWAPFILE) != 0
   )
      return;      // nothing to do
      
   //There is a small chance that between choosing the swap file name and creating it, another
   //Eegl creates the file. In that case the creation will fail and we will use another directory
   CS fname = findSwapName(book, NULL); // allocates fname
   if (mf_open_file(mfp, fname) == OK) {// consumes fname!
      // don't sync yet in ml_sync_all()
      mfp->mf_dirty = MF_DIRTY_YES_NOSYNC;
      updateBlock0(book, UB_SAME_DIR);

      //Flush block zero, so others can read it
      if (mf_sync(mfp, MFS_ZERO) == OK) {
         //Mark all blocks that should be in the swapfile as dirty. Needed for when the 
         //@swapfile was reset, so that the swap file was deleted, and then on again.
         mf_set_dirty(mfp);
         goto success;
      }
      // Writing block 0 failed: close the file and try another dir
      mf_close_file(book, false);
   }

success:
   if (!mfp->fName) {
      need_wait_return = true;   // call wait_return() later
      ++no_wait_return;
      (void)showErrFmtMsg(_(e_unable_to_open_swap_file_for_str_recovery_impossible),
             bookSpName(book) != NULL ? bookSpName(book) : book->currFileName
      );
      --no_wait_return;
   }

   // don't try to open a swap file again
   book->maySwap = false;
}

//If still need to create a swap file, and starting to edit a not-readonly
//file, or reading into an existing book, create a swap file now.
pub void
check_need_swap(int newfile)  {    // reading file into new book
   int old_msg_silent = msg_silent; // might be reset by an E325 message

   if (curBook->maySwap && (curBook->o.modifiable || !newfile))
      memOpenSwapFile(curBook);
   msg_silent = old_msg_silent;
}

// Close memline for book. If 'del_file' is true, delete the swap file
pub void
ml_close(Book* book, Boole del_file) {
   if (book->mem.mfile == NULL)      // not open
      return;
      
   mf_close(book->mem.mfile, del_file);   // close the .swp file
   if (book->mem.ml_line_lnum != 0 && (book->mem.flags & ML_LINE_DIRTY))
      eeglFree(book->mem.cachedLine);
   eeglFree(book->mem.ml_stack);
   EE_CLEAR(book->mem.ml_chunksize);
   book->mem.mfile = NULL;

   // Reset the "recovered" flag, give the ATTENTION prompt the next time this book is loaded.
   book->flags &= ~BF_RECOVERED;
}

// Close all existing memlines and memfiles. Only used when exiting.
// When 'del_file' is true, delete the memfiles.
// But don't delete files that were ":preserve"d when we are POSIX compatible.
pub void
ml_close_all(Boole del_file) {
   Book* book;
   FOR_ALL_BOOKS(book) {
      ml_close(book, del_file);
   } 
   eeDelTempDir();      // delete created temp directory
}

// Close all memfiles for not modified books. Only use just before exiting!
pub void
ml_close_notmod(void) {
   Book* book;
   FOR_ALL_BOOKS(book) {
      if (!bookWasChanged(book))
         ml_close(book, true);    // close all not-modified books
   } 
}

// Update the timestamp in the .swp file. Used when the file has been written.
pub void
ml_timestamp(Book* book) {
   updateBlock0(book, UB_FNAME);
}

// Return FAIL when the ID of "b0p" is wrong.
private int
ml_check_b0_id(Block0* b0p) {
   if (b0p->b0_id[0] != BLOCK0_ID0
       || (b0p->b0_id[1] != BLOCK0_ID1
         && b0p->b0_id[1] != BLOCK0_ID1_C0
         && b0p->b0_id[1] != BLOCK0_ID1_C1
         && b0p->b0_id[1] != BLOCK0_ID1_C2
         && b0p->b0_id[1] != BLOCK0_ID1_C3
         && b0p->b0_id[1] != BLOCK0_ID1_C4)
   )
      return FAIL;
   return OK;
}

// Update the timestamp or the B0_SAME_DIR flag of the .swp file.
private void
updateBlock0(Book *book, UpdBlock0 what) {
   MemFile* mfp = book->mem.mfile;
   if (!mfp)
      return;
   BlockHeader* hdr = mf_get(mfp, (BlockId)0, 1);
   if (!hdr) {
      return;
   }

   Block0* b0p = (Block0 *)(hdr->bh_data);
   if (ml_check_b0_id(b0p) == FAIL)
      internalErrMsg(e_ml_upd_block0_didnt_get_block_zero);
   else {
      if (what == UB_FNAME)
         set_b0_fname(b0p, book);
      else // what == UB_SAME_DIR
         set_b0_dir_flag(b0p, book);
   }
   mf_put(mfp, hdr, true, false);
}

//Write file name and timestamp into block 0 of a swap file. Also set book->modifiedTime.
//Don't use nameBuffG[]!!!
private void
set_b0_fname(Block0 *b0p, Book *book) {

   if (book->fullFileName == NULL)
      b0p->b0_fname[0] = ZERO;
   else {
      Unt   flen, ulen;
      Byte   uname[B0_UNAME_SIZE];

      //For a file under the home directory of the current user, we try to replace the home 
      //directory path with "~user". This helps when editing the same file on different machines 
      //over a network. First replace home dir path with "~/" with home_replace().
      //Then insert the user name to get "~user/".
      home_replace(book->fullFileName, b0p->b0_fname, B0_FNAME_SIZE_CRYPT, true);
      if (b0p->b0_fname[0] == '~') {
         flen = STRLEN(b0p->b0_fname);
         // If there is no user name or it is too long, don't use "~/"
         if (get_user_name(uname, B0_UNAME_SIZE) == FAIL
               || (ulen = STRLEN(uname)) + flen > B0_FNAME_SIZE_CRYPT - 1
         ){
            copySubstrToAllocation(
               b0p->b0_fname, (Text){book->fullFileName, B0_FNAME_SIZE_CRYPT - 1}
            );
         } else {
            MEMMOVE(b0p->b0_fname + ulen + 1, b0p->b0_fname + 1, flen);
            MEMMOVE(b0p->b0_fname + 1, uname, ulen);
         }
      }
      
      FileStat   st;
      if (stat((char *)book->fullFileName, &st) >= 0) {
         longToChar((long)st.st_mtime, b0p->b0_mtime);
         longToChar((long)st.st_ino, b0p->b0_ino);
         buf_store_time(book, &st, book->fullFileName);
         book->readTime = book->modifiedTime;
         book->readTimeNs = book->modifiedTimeNs;
      } else {
         longToChar(0L, b0p->b0_mtime);
         longToChar(0L, b0p->b0_ino);
         book->modifiedTime = 0;
         book->modifiedTimeNs = 0;
         book->readTime = 0;
         book->readTimeNs = 0;
         book->origSize = 0;
         book->origMode = 0;
      }
   }
}

// Update the B0_SAME_DIR flag of the swap file.  It's set if the file and the
// swapfile for "book" are in the same directory.
// This is fail safe: if we are not sure the directories are equal the flag is not set.
private void
set_b0_dir_flag(Block0* b0p, Book* book) {
   if (same_directory(book->mem.mfile->fName, book->fullFileName))
      b0p->b0_flags |= B0_SAME_DIR;
   else
      b0p->b0_flags &= ~B0_SAME_DIR;
}


//Return true if the process with number "b0p->b0_pid" is still running. "swap_fname" is the name 
//of the swap file, if it's from before a reboot then the result is false.
private int
swapfile_process_running(Block0 *b0p, CS swap_fname) {
   FileStat st;
   struct sysinfo sinfo;

   //If the system rebooted after when the swap file was written then the
   //process can't be running now.
   if (STAT(swap_fname, &st) != -1
          && sysinfo(&sinfo) == 0
          && st.st_mtime 
             < time(NULL) - (overrideSysinfoUptimeG >= 0 ? overrideSysinfoUptimeG : sinfo.uptime)
   )
      return false;
   return mch_process_running(charToLong(b0p->b0_pid));
}

// Try to recover curBook from the .swp file.
// If "checkext" is true, check the extension and detect whether it is a swap file.
pub void
ml_recover(Boole checkext) {
   Book* book = NULL;
   MemFile* mfp = NULL ;
   CS fname_used = NULL;
   BlockHeader* hdr = NULL;
   Block0* b0p;
   PointerBlock* pp;
   DataBlock* block;
   InfoPtr* ip;
   FileStat org_stat, swp_stat;
   int len;
   Boole directly;
   int i;
   int has_error;
   int top;
   int txt_start;
   FileOffset size;
   Boole serious_error = true;
   int orig_file_status = NOTDONE;

   recoveryModeG = true;
   Boole called_from_main = (curBook->mem.mfile == NULL);
   char deco = getDecoFlags(HLF_E);

   //If the file name ends in ".swp", we assume this is the swap file.
   //Otherwise a search is done to find the swap file(s).
   CS fname = curBook->fullFileName;
   if (!fname)          // When there is no file name
      fname = S"";
   len = (int)STRLEN(fname);
   if (checkext && len >= 4 && STRNICMP(fname + len - 4, ".swp", 4) == 0) {
      directly = true;
      fname_used = copyStr(fname); // make a copy for mf_open()
   } else {
      directly = false;

      // count the number of matching swap files
      fname_used = fiBuildSwapOrUndoFname(fname, false);
      if (!fname_used) {  // no swap files found
         showErrFmtMsg(_(e_no_swap_file_found_for_str), fname);
         goto theend;
      }
   }
   if (!fname_used)
      goto theend;         // out of memory

   // When called from main() still need to initialize storage structure
   if (called_from_main && ml_open(curBook) == FAIL)
      exitEegl(1);

   //Allocate a book structure for the swap file that is used for recovery.
   //Only the memline and crypt information in it are really used.
   book = ALLOC_ONE(Book);

   // init fields in memline struct
   book->mem.ml_stack_size = 0;   // no stack yet
   book->mem.ml_stack = NULL;      // no stack yet
   book->mem.ml_stack_top = 0;      // nothing in the stack
   book->mem.ml_line_lnum = 0;      // no cached line
   book->mem.locked = NULL;      // no locked block
   book->mem.flags = 0;

   // open the memfile from the old swap file
   CS p = copyStr(fname_used); // save "fname_used" for the message:
             // mf_open() will consume "fname_used"!
   mfp = mf_open(fname_used, O_RDONLY);
   fname_used = p;
   if (!mfp || mfp->fd < 0) {
      if (fname_used != NULL)
         showErrFmtMsg(_(e_cannot_open_str), fname_used);
      goto theend;
   }
   book->mem.mfile = mfp;
   mfp->book = book;

   //The page size set in mf_open() might be different from the page size used in the swap file, we 
   //must get it from block 0.  But to read block 0 we need a page size.  Use the minimal size for 
   //block 0 here, it will be set to the real value below.
   mfp->pageSize = MIN_SWAP_PAGE_SIZE;

   // try to read block 0
   if ((hdr = mf_get(mfp, (BlockId)0, 1)) == NULL) {
      msg_start();
      msgPutsDeco(_("Unable to read block 0 from "), deco | MSG_HIST);
      msgOuttransDeco(mfp->fName, deco | MSG_HIST);
      msgPutsDeco(_("\nMaybe no changes were made or Eegl did not update the swap file."),
         deco | MSG_HIST);
      msg_end();
      goto theend;
   }
   b0p = (Block0 *)(hdr->bh_data);
   if (ml_check_b0_id(b0p) == FAIL) {
      showErrFmtMsg(_(e_str_does_not_look_like_eegl_swap_file), mfp->fName);
      goto theend;
   }
   if (b0_magic_wrong(b0p)) {
      msg_start();
      msgOuttransDeco(mfp->fName, deco | MSG_HIST);
      msgPutsDeco(_(" cannot be used on this computer.\n"), deco | MSG_HIST);
      msgPutsDeco(_("The file was created on "), deco | MSG_HIST);
      // avoid going past the end of a corrupted hostname
      b0p->b0_fname[0] = ZERO;
      msgPutsDeco(b0p->b0_hname, deco | MSG_HIST);
      msgPutsDeco(_(",\nor the file has been damaged."), deco | MSG_HIST);
      msg_end();
      goto theend;
   }

   //If we guessed the wrong page size, we have to recalculate the highest block number in the file
   if (mfp->pageSize != (unsigned)charToLong(b0p->b0_page_size)) {
      unsigned previous_page_size = mfp->pageSize;

      mf_new_page_size(mfp, (unsigned)charToLong(b0p->b0_page_size));
      if (mfp->pageSize < previous_page_size) {
          msg_start();
          msgOuttransDeco(mfp->fName, deco | MSG_HIST);
          msgPutsDeco(_(" has been damaged (page size is smaller than minimum value).\n"),
            deco | MSG_HIST);
          msg_end();
          goto theend;
      }
      if ((size = lseek(mfp->fd, (FileOffset)0L, SEEK_END)) <= 0)
          mfp->mf_blocknr_max = 0;       // no file or empty file
      else
          mfp->mf_blocknr_max = (BlockId)(size / mfp->pageSize);
      mfp->pagesInFile = mfp->mf_blocknr_max;

      // need to reallocate the memory used to store the data
      p = alloc(mfp->pageSize);
      MEMMOVE(p, hdr->bh_data, previous_page_size);
      eeglFree(hdr->bh_data);
      hdr->bh_data = p;
      b0p = (Block0 *)(hdr->bh_data);
   }

   // If .swp file name given directly, use name from swap file for book.
   if (directly) {
      doExpandEnv(OUT nameBuffTextG, b0p->b0_fname);
      if (setfname(curBook, nameBuffG, NULL, true) == FAIL)
          goto theend;
   }

   home_replace(mfp->fName, nameBuffG, MAXPATHL, true);
   smsg(_("Using swap file \"%s\""), nameBuffG);

   if (bookSpName(curBook) != NULL)
      copySubstrToAllocation(nameBuffG, (Text){bookSpName(curBook), MAXPATHL - 1});
   else
      home_replace(curBook->fullFileName, nameBuffG, MAXPATHL, true);
   smsg(_("Original file \"%s\""), nameBuffG);
   msg_putchar('\n');

   // check date of swap file and original file
   long mtime = charToLong(b0p->b0_mtime);
   if (curBook->fullFileName != NULL
          && stat((char *)curBook->fullFileName, &org_stat) != -1
          && ((stat((char *)mfp->fName, &swp_stat) != -1
             && org_stat.st_mtime > swp_stat.st_mtime)
         || org_stat.st_mtime != mtime))
      emsg(_(e_warning_original_file_may_have_been_changed));
   out_flush();


   mf_put(mfp, hdr, false, false);   // release block 0
   hdr = NULL;

   //Now that we are sure that the file is going to be recovered, clear the
   //contents of the current book.
   while (!(curBook->mem.flags & ML_EMPTY))
   ml_delete((LineNr)1);

   //Try reading the original file to obtain the values of 'binary'. Ignore errors. The text 
   //itself is not used. When the file is encrypted the user is asked to enter the key.
   if (curBook->fullFileName)
      orig_file_status = readfile(
            curBook->fullFileName, NULL, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, NULL, READ_NEW
      );

   unchanged(curBook, true);

   BlockId bnum = 1;      // start with block 1
   int pageCount = 1;   // which is 1 page
   LineNr lnum = 0;      // append after line 0 in curBook
   LineNr lineCount = 0;
   int idx = 0;      // start with first index in block 1
   long error = 0;
   book->mem.ml_stack_top = 0;
   book->mem.ml_stack = NULL;
   book->mem.ml_stack_size = 0;   // no stack yet

   Boole cannotOpen = (curBook->fullFileName == NULL);

   serious_error = false;
   for ( ; !gotInterruptG; line_breakcheck()) {
      if (hdr)
         mf_put(mfp, hdr, false, false);   // release previous block

      // get block
      if ((hdr = mf_get(mfp, bnum, pageCount)) == NULL) {
         if (bnum == 1) {
            showErrFmtMsg(_(e_unable_to_read_block_one_from_str), mfp->fName);
            goto theend;
         }
         ++error;
         ml_append(lnum++, (CS)_("???MANY LINES MISSING"), (ColNr)0, true);
      } else   {// there is a block
         pp = (PointerBlock *)(hdr->bh_data);
         if (pp->id == PTR_ID) { // it is a pointer block
            int PointerBlockock_error = false;
            if (pp->pointerCountMax != pointerCountMax(mfp)) {
                PointerBlockock_error = true;
                pp->pointerCountMax = pointerCountMax(mfp);
            }
            if (pp->pointerCount > pp->pointerCountMax) {
                PointerBlockock_error = true;
                pp->pointerCount = pp->pointerCountMax;
            }
            if (PointerBlockock_error)
                emsg(_(e_warning_pointer_block_corrupted));

            // check line count when using pointer block first time
            if (idx == 0 && lineCount != 0) {
               for (i = 0; i < (int)pp->pointerCount; ++i)
                  lineCount -= pp->c[i].lineCount;
               if (lineCount != 0) {
                  ++error;
                  ml_append(lnum++, (CS)_("???LINE COUNT WRONG"), (ColNr)0, true);
               }
            }

            if (pp->pointerCount == 0) {
               ml_append(lnum++, (CS)_("???EMPTY BLOCK"), (ColNr)0, true);
               ++error;
            } ei (idx < (int)pp->pointerCount) {// go a block deeper
               if (pp->c[idx].blockId < 0) {
                  // Data block with negative block number. Try to read lines from the original file.
                  // This is slow, but it works.
                  if (!cannotOpen) {
                     lineCount = pp->c[idx].lineCount;
                     if (readfile(
                                curBook->fullFileName, NULL, lnum, pp->c[idx].oldLnum - 1, lineCount, 
                                NULL, 0
                           ) != OK)
                        cannotOpen = true;
                     else
                        lnum += lineCount;
                  }
                  if (cannotOpen) {
                      ++error;
                      ml_append(lnum++, (CS)_("???LINES MISSING"), (ColNr)0, true);
                  }
                  ++idx;       // get same block again for next index
                  continue;
               }

               //going one block deeper in the tree
               if ((top = ml_add_stack(book)) < 0) {// new entry in stack
                  ++error;
                  break;          // out of memory
               }
               ip = &(book->mem.ml_stack[top]);
               ip->ip_bnum = bnum;
               ip->ip_index = idx;

               bnum = pp->c[idx].blockId;
               lineCount = pp->c[idx].lineCount;
               pageCount = pp->c[idx].pageCount;
               idx = 0;
               continue;
            }
         } else {    // not a pointer block
            block = (DataBlock *)(hdr->bh_data);
            if (block->id != DATA_ID) {// block id wrong
               if (bnum == 1) {
                  showErrFmtMsg(_(e_block_one_id_wrong_str_not_swp_file), mfp->fName);
                  goto theend;
               }
               ++error;
               ml_append(lnum++, (CS)_("???BLOCK MISSING"), (ColNr)0, true);
            } else {
               //It is a data block. Append all the lines in this block.
               has_error = false;

               //Check the length of the block. If wrong, use the length given in the pointer block
               if (pageCount * mfp->pageSize != block->endByte) {
                  ml_append(lnum++, 
                     (CS)_("??? from here until ???END lines may be messed up"),
                     (ColNr)0, true
                  );
                  ++error;
                  has_error = true;
                  block->endByte = pageCount * mfp->pageSize;
               }

               //Make sure there is a ZERO at the end of the block so we
               //don't go over the end when copying text.
               *((CS)block + block->endByte - 1) = ZERO;

               //Check the number of lines in the block.
               //If wrong, use the count in the data block.
               if (lineCount != block->countLines) {
                  ml_append(lnum++, 
                     (CS)_("??? from here until ???END" " lines may have been inserted/deleted"),
                     (ColNr)0, true
                  );
                  ++error;
                  has_error = true;
               }

               int did_questions = false;
               for (i = 0; i < block->countLines; ++i) {
                  if ((CS)&(block->c[i]) >= (CS)block + block->startByte) {
                     // line count must be wrong
                     ++error;
                     ml_append(
                         lnum++, (CS)_("??? lines may be missing"), (ColNr)0, true
                     );
                     break;
                  }

                  txt_start = (block->c[i] & c_MASK);
                  if (txt_start <= (int)HEADER_SIZE || txt_start >= (int)block->endByte) {
                     ++error;
                     // avoid lots of lines with "???"
                     if (did_questions)
                        continue;
                     did_questions = true;
                     p = (CS)"???";
                  } else {
                     did_questions = false;
                     p = (CS)block + txt_start;
                  }
                  ml_append(lnum++, p, (ColNr)0, true);
               }
               if (has_error)
                  ml_append(lnum++, (CS)_("???END"), (ColNr)0, true);
            }
         }
      }

      if (book->mem.ml_stack_top == 0)   // finished
          break;

      // go one block up in the tree
      ip = &(book->mem.ml_stack[--(book->mem.ml_stack_top)]);
      bnum = ip->ip_bnum;
      idx = ip->ip_index + 1;       // go to next index
      pageCount = 1;
   }

   //Compare the book contents with the original file. When they differ set the 'modified' flag.
   //Lines 1 - lnum are the new contents.
   //Lines lnum + 1 to lineCount are the original contents.
   //Line lineCount + 1 in the dummy empty line.
   if (orig_file_status != OK || curBook->mem.lineCount != lnum * 2 + 1) {
      //Recovering an empty file results in two lines and the first line is
      //empty. Don't set the modified flag then.
      if (!(curBook->mem.lineCount == 2 && *ml_get(1) == ZERO)) {
         doOnChangeToText();
         ++CHANGEDTICK(curBook);
      }
   } else {
      for (idx = 1; idx <= lnum; ++idx) {
         // Need to copy one line, fetching the other one may flush it.
         p = copySubstr(ml_get(idx), ml_get_len(idx));
         i = STRCMP(p, ml_get(idx + lnum));
         eeglFree(p);
         if (i != 0) {
            doOnChangeToText();
            ++CHANGEDTICK(curBook);
            break;
         }
      }
   }

   // Delete the lines from the original file and the dummy line from the
   // empty book.  These will now be after the last line in the book.
   while (curBook->mem.lineCount > lnum && !(curBook->mem.flags & ML_EMPTY))
      ml_delete(curBook->mem.lineCount);
   curBook->flags |= BF_RECOVERED;
   check_cursor();

   recoveryModeG = false;
   if (gotInterruptG)
      emsg(_(e_recovery_interrupted));
   ei (error) {
      ++no_wait_return;
      msg(S">>>>>>>>>>>>>");
      emsg(_(e_errors_detected_while_recovering_look_for_lines_starting_with_questions));
      --no_wait_return;
      msg(_("See \":help E312\" for more information."));
      msg(S">>>>>>>>>>>>>");
   } else {
      if (curBook->wasModified) {
          msg(_("Recovery completed. You should check if everything is OK."));
          msg_puts(_("\n(You might want to write out this file under another name\n"));
          msg_puts(_("and run diff with the original file to check for changes)"));
      } else
          msg(_("Recovery completed. Book contents equals file contents."));
      msg_puts(_("\nYou may want to delete the .swp file now."));
      if (swapfile_process_running(b0p, fname_used)) {
          // Warn there could be an active Eegl on the same file, the user may want to kill it.
          msg_puts(_("\nNote: process STILL RUNNING: "));
          msg_outnum(charToLong(b0p->b0_pid));
      }
      msg_puts(S"\n\n");
      commlineRowG = msgRowG;
   }
   drawCurBookLater(UPD_NOT_VALID);

theend:
   eeglFree(fname_used);
   recoveryModeG = false;
   if (mfp) {
      if (hdr)
         mf_put(mfp, hdr, false, false);
      mf_close(mfp, false);       // will also eeglFree(mfp->fName)
   }
   if (book) {
      eeglFree(book->mem.ml_stack);
      eeglFree(book);
   }
   if (serious_error && called_from_main)
      ml_close(curBook, true);
   else {
      applyAutocomms(EVENT_BUFREADPOST, NULL, curBook->currFileName, false, curBook);
      applyAutocomms(EVENT_BUFWINENTER, NULL, curBook->currFileName, false, curBook);
   }
}

private int process_still_running;

// Return information found in swapfile "fname" in dictionary "d".
// This is used by the swapinfo() function.
pub void
get_b0_dict(CS fname, Bag *bag) {
   Block0 b0;
   int fd;
   if ((fd = open((char *)fname, O_RDONLY | O_EXTRA, 0)) >= 0) {
      if (fiReadEintr(fd, &b0, sizeof(b0)) == sizeof(b0)) {
         if (ml_check_b0_id(&b0) == FAIL)
            bagAddString(bag, S"error", S"Not a swap file");
         ei (b0_magic_wrong(&b0))
            bagAddString(bag, S"error", S"Magic number mismatch");
         else {
            // we have swap information
            bagAddString_len(bag, S"version", b0.b0_version, 10);
            bagAddString_len(bag, S"user", b0.b0_uname, B0_UNAME_SIZE);
            bagAddString_len(bag, S"host", b0.b0_hname, B0_HNAME_SIZE);
            bagAddString_len(bag, S"fname", b0.b0_fname, B0_FNAME_SIZE_ORG);

            bagAddNumber(bag, S"pid", charToLong(b0.b0_pid));
            bagAddNumber(bag, S"mtime", charToLong(b0.b0_mtime));
            bagAddNumber(bag, S"dirty", b0.b0_dirty ? 1 : 0);
            bagAddNumber(bag, S"inode", charToLong(b0.b0_ino));
         }
      } else
         bagAddString(bag, S"error", (CS)"Cannot read file");
      close(fd);
   } else
      bagAddString(bag, S"error", (CS)"Cannot open file");
}

// Return true if the swap file looks OK and there are no changes, thus it can be safely deleted.
private int
swapfile_unchanged(CS fname) {
   FileStat st;
   int ret = true;

   // must be able to stat the swap file
   if (stat((char *)fname, &st) == -1)
      return false;

   // must be able to read the first block
   int fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
   if (fd < 0)
      return false;
   Block0   b0;
   if (fiReadEintr(fd, OUT &b0, sizeof(b0)) != sizeof(b0)) {
      close(fd);
      return false;
   }

   // the ID and magic number must be correct
   if (ml_check_b0_id(&b0) == FAIL|| b0_magic_wrong(&b0))
      ret = false;

   // must be unchanged
   if (b0.b0_dirty)
      ret = false;

   // Host name must be known and must equal the current host name, otherwise
   // comparing pid is meaningless.
   if (*(b0.b0_hname) == ZERO) {
      ret = false;
   } else {
      Byte hostname[B0_HNAME_SIZE];

      mch_get_host_name(hostname, B0_HNAME_SIZE);
      hostname[B0_HNAME_SIZE - 1] = ZERO;
      b0.b0_hname[B0_HNAME_SIZE - 1] = ZERO; // in case of corruption
      if (caseInsensitiveCompare(b0.b0_hname, hostname) != 0)
         ret = false;
   }

   // process must be known and not be running
   if (charToLong(b0.b0_pid) == 0L || swapfile_process_running(&b0, fname))
      ret = false;

   // We do not check the user, it should be irrelevant for whether the swap file is still useful
   close(fd);
   return ret;
}

//sync all memlines
//
//If 'check_file' is true, check if original file exists and was not changed.
//If 'check_char' is true, stop syncing when character becomes available, but
//always sync at least one block.
pub void
ml_sync_all(int check_file, int check_char) {
   Book* book;
   FileStat st;

   FOR_ALL_BOOKS(book) {
      if (!book->mem.mfile || !book->mem.mfile->fName || book->mem.mfile->fd < 0)
         continue;             // no file

      flushLine(book); // flush buffered line
                      // flush locked block
      (void)ml_find_line(book, (LineNr)0, ML_FLUSH);
      if (bookWasChanged(book) 
            && check_file && mf_need_trans(book->mem.mfile)
            && book->fullFileName
      ){
         //If the original file does not exist anymore or has been changed
         //call ml_preserve() to get rid of all negative numbered blocks.
         if (stat((char *)book->fullFileName, &st) == -1
             || st.st_mtime != book->readTime
#ifdef ST_MTIM_NSEC
             || st.ST_MTIM_NSEC != book->readTimeNs
#endif
             || st.st_size != book->origSize
         ) {
            ml_preserve(book, false);
            did_check_timestamps = false;
            need_check_timestamps = true;   // give message later
         }
      }
      if (book->mem.mfile->mf_dirty == MF_DIRTY_YES) {
         (void)mf_sync(
            book->mem.mfile, (check_char ? MFS_STOP : 0) | (bookWasChanged(book) ? MFS_FLUSH : 0)
         );
         if (check_char && ui_char_avail())   // character available now
            break;
      }
   }
}

//Sync one book, including negative blocks. After this all the blocks are in the swap file
//Used for the :preserve command and when the original file has been changed or deleted.
//when message is true the success of preserving is reported
pub void
ml_preserve(Book* book, int message) {
   BlockHeader* hdr;
   LineNr lnum;
   MemFile* mfp = book->mem.mfile;
   int status;
   int gotInterruptG_save = gotInterruptG;

   if (!mfp || !mfp->fName) {
      if (message)
         emsg(_(e_cannot_preserve_there_is_no_swap_file));
      return;
   }

   // We only want to stop when interrupted here, not when interrupted before.
   gotInterruptG = false;

   flushLine(book);                // flush buffered line
   (void)ml_find_line(book, (LineNr)0, ML_FLUSH); // flush locked block
   status = mf_sync(mfp, MFS_ALL | MFS_FLUSH);

   // stack is invalid after mf_sync(.., MFS_ALL)
   book->mem.ml_stack_top = 0;

   //Some of the data blocks may have been changed from negative to positive block number. 
   //In that case the pointer blocks need to be updated.
   //
   //We don't know in which pointer block the references are, so we visit all data blocks until 
   //there are no more translations to be done (or we hit the end of the file, which can only 
   //happen in case a write fails, e.g. when file system if full). ml_find_line() does the work 
   //by translating the negative block numbers when getting the first line of each data block.
   if (mf_need_trans(mfp) && !gotInterruptG) {
      lnum = 1;
      while (mf_need_trans(mfp) && lnum <= book->mem.lineCount) {
         hdr = ml_find_line(book, lnum, ML_FIND);
         if (!hdr) {
            status = FAIL;
            goto theend;
         }
         CHECK(book->mem.lockedLow != lnum, "low != lnum");
         lnum = book->mem.lockedHigh + 1;
      }
      (void)ml_find_line(book, (LineNr)0, ML_FLUSH);   // flush locked block
      // sync the updated pointer blocks
      if (mf_sync(mfp, MFS_ALL | MFS_FLUSH) == FAIL)
         status = FAIL;
      book->mem.ml_stack_top = 0;       // stack is invalid now
   }
theend:
   gotInterruptG |= gotInterruptG_save;

   if (message) {
      if (status == OK)
         msg(_("File preserved"));
      else
         emsg(_(e_preserve_failed));
   }
}

// NOTE: The pointer returned by the ml_get_*() functions only remains valid
// until the next call!
//  line1 = ml_get(1);
//  line2 = ml_get(2);   // line1 is now invalid!
// Make a copy of the line if necessary.
// 
// Return a pointer to a (read-only copy of a) line in the current book.
//
// On failure an error message is given and IObuff is returned (to avoid
// having to check for error everywhere).
pub CS
ml_get(LineNr lnum) {
   return memGetLine(curBook, lnum, false);
}

// Return pointer to position "pos".
pub CS
ml_get_pos(Pos* pos){
   return (memGetLine(curBook, pos->lnum, false) + pos->col);
}

// Return pointer to cursor line.
pub CS
ml_get_curline(void) {
   return memGetLine(curBook, curPor->cursor.lnum, false);
}

// Return pointer to cursor position.
pub CS
ml_get_cursor(void) {
   return memGetLine(curBook, curPor->cursor.lnum, false) + curPor->cursor.col;
}

// return length (excluding the ZERO) of the given line
pub ColNr
ml_get_len(LineNr lnum) {
   return memGetBookLen(curBook, lnum);
}

// return length (excluding the ZERO) of the text after position "pos"
pub ColNr
ml_get_pos_len(Pos *pos) {
   return memGetBookLen(curBook, pos->lnum) - pos->col;
}

// return length (excluding the ZERO) of the cursor line
pub ColNr
ml_get_curline_len(void) {
   return memGetBookLen(curBook, curPor->cursor.lnum);
}

// return length (excluding the ZERO) of the cursor position
pub ColNr
ml_get_cursor_len(void) {
   return memGetBookLen(curBook, curPor->cursor.lnum) - curPor->cursor.col;
}

// return length (excluding the ZERO) of the given line in the given book
pub ColNr
memGetBookLen(Book* book, LineNr lnum) {
   CS line = memGetLine(book, lnum, false); if (*line == ZERO)
      return 0;

   if (book->mem.lineTextLen <= 0)
      book->mem.lineTextLen = (int)STRLEN(line) + 1;
   return book->mem.lineTextLen - 1;
}

//Return a pointer to a line in a specific book
//"willChange": if true mark the book dirty (chars in the line are expected to change)
pub CS
memGetLine(Book* book, LineNr lnum, Boole willChange) { // line will be changed
   BlockHeader* hdr;
   DataBlock   *block;
   static int   recursive = 0;
   static Byte questions[4];

   if (lnum > book->mem.lineCount) { // invalid line number
      if (recursive == 0) {
         //Avoid giving this message for a recursive call, may happen when
         //the GUI redraws part of the text.
         ++recursive;
         internalErrFmtMsg(e_ml_get_invalid_lnum_nr, lnum);
         --recursive;
      }
      flushLine(book);
errorret:
      STRCPY(questions, "???");
      book->mem.lineLen = 4;
      book->mem.lineTextLen = book->mem.lineLen;
      book->mem.ml_line_lnum = lnum;
      return questions;
   }
   if (lnum <= 0)         // pretend line 0 is line 1
      lnum = 1;

   if (book->mem.mfile == NULL) {// there are no lines
      book->mem.lineLen = 1;
      book->mem.lineTextLen = book->mem.lineLen;
      return S"";
   }

   //See if it is the same line as requested last time. Otherwise may need to flush last used line.
   //Don't use the last used line when 'swapfile' is reset, need to load all blocks.
   if (book->mem.ml_line_lnum != lnum || dontReleaseBlocksS) {
      Unt start, end;
      flushLine(book);

      //Find the data block containing the line. This also fills the stack with the blocks from 
      //the root to the data block and releases any locked block.
      if ((hdr = ml_find_line(book, lnum, ML_FIND)) == NULL) {
         if (recursive == 0) {
            //Avoid giving this message for a recursive call, may happen
            //when the UI redraws part of the text.
            ++recursive;
            drawGetTranslatedBookName(book);
            shorten_dir(nameBuffG);
            internalErrFmtMsg(
               e_ml_get_cannot_find_line_nr_in_buffer_nr_str, lnum, book->fiNum, nameBuffG
            );
            --recursive;
         }
         goto errorret;
      }

      block = (DataBlock *)(hdr->bh_data);

      int idx = lnum - book->mem.lockedLow;
      start = ((block->c[idx]) & c_MASK);
      // The text ends where the previous line starts. The first line ends at the end of the block
      if (idx == 0)
         end = block->endByte;
      else
         end = ((block->c[idx - 1]) & c_MASK);

      book->mem.cachedLine = (CS)block + start;
      book->mem.lineLen = end - start;
      // Text properties come after a ZERO byte, so lineLen should be
      // larger than the size of TextProp if there is any.
      if (book->hasTextprop && (Unt)book->mem.lineLen > sizeof(TextProp))
         book->mem.lineTextLen = 0;  // call STRLEN() later when needed
      else
         book->mem.lineTextLen = book->mem.lineLen;
      book->mem.ml_line_lnum = lnum;
      book->mem.flags &= ~ML_LINE_DIRTY;
   }
   if (willChange) {
      book->mem.flags |= (ML_LOCKED_DIRTY | ML_LOCKED_POS);
   }

   return book->mem.cachedLine;
}

//Check if a line that was just obtained by a call to ml_get is in allocated memory.
pub int
ml_line_alloced(void) {
   return (curBook->mem.flags & ML_LINE_DIRTY);
}

//Add text properties that continue from the previous line.
private void
addTextPropsForAppend(
   Book* book,
   LineNr   lnum,
   OUT CS* lineContent,
   OUT int* len,
   OUT Byte** tofree
){
   int      round;
   int      newPropCount = 0;
   int      count;
   int      n;
   int      newLen = 0;
   Arr(Byte) newLineContent = NULL;
   TextProp   prop;

   // Make two rounds:
   // 1. calculate the extra space needed
   // 2. allocate the space and fill it
   for (round = 1; round <= 2; ++round) {
      if (round == 2) {
         if (newPropCount == 0)
            return;  // nothing to do
         newLen = *len + newPropCount * sizeof(TextProp);
         newLineContent = alloc(newLen);
         if (newLineContent == NULL)
            return;
         if (*len > 0) {
            MEMMOVE(newLineContent, *lineContent, *len);
         }
         newPropCount = 0;
      }

      // Get the line above to find any props that continue in the next line.
      CS props;
      count = get_text_props(OUT &props, book, lnum, false);
      for (n = 0; n < count; ++n) {
         MEMMOVE(&prop, props + n * sizeof(TextProp), sizeof(TextProp));
         if (prop.flags & TEXT_PROP_CONT_NEXT) {
            if (round == 2) {
               prop.flags |= TEXT_PROP_CONT_PREV;
               prop.col = 1;
               prop.len = *len;  // not exactly the right length
               MEMMOVE(
                 newLineContent + *len + newPropCount * sizeof(TextProp), 
                 &prop, 
                 sizeof(TextProp)
               );
            }
            ++newPropCount;
         }
      }
   }
   *lineContent = newLineContent;
   *tofree = newLineContent;
   *len = newLen;
}

// Insert a new line with text at an arbitrary line number
private int
insertLineText(
   Book* book,
   LineNr lnum,      // append after this line (can be 0)
   CS newContentArg, // text of the new line
   ColNr lenArgWithZeroChar,   // length of line, including ZERO, or 0
   Unt flags      // ML_APPEND_ flags
){
   Arr(Byte) newContent = newContentArg;
   ColNr len = lenArgWithZeroChar;
   int i;
   int lineCount;   // number of indexes in current block
   int offset;
   int from, to;
   int neededSpace; // space needed for new line
   int page_size;
   int pageCount;
   int oldLineInd;   // index for lnum in data block
   BlockHeader* hdr;
   PointerBlock* pp;
   InfoPtr* ip;
   Byte* tofree = NULL;
   ColNr textLen = 0;   // text len with ZERO without text properties
   int ret = FAIL;

   if (lnum > book->mem.lineCount || book->mem.mfile == NULL)
      return FAIL;  // lnum out of range

   if (lowest_marked && lowest_marked > lnum)
      lowest_marked = lnum + 1;

   if (len == 0) {
      len = 1;   // space needed for the text
      textLen = len;
   } ei (curBook->hasTextprop) {
      // "len" may include text properties, get the length of the text.
      textLen = (ColNr)STRLEN(newContent) + 1;
   } else {
      textLen = len + 1;
   } 

   if (curBook->hasTextprop && lnum > 0 && !(flags & (ML_APPEND_UNDO | ML_APPEND_NOPROP))) {
      // Add text properties that continue from the previous line.
      addTextPropsForAppend(book, lnum, OUT &newContent, OUT &len, OUT &tofree);
   }

   neededSpace = len + INDEX_SIZE;   // space needed for text + index

   MemFile* mfp = book->mem.mfile;
   page_size = mfp->pageSize;

   //Find the data block containing the previous line. This also fills the stack with the blocks
   //from the root to the data block, and releases any locked block.
   if ((hdr = ml_find_line(book, lnum == 0 ? (LineNr)1 : lnum, ML_INSERT)) == NULL)
      goto theend;

   book->mem.flags &= ~ML_EMPTY;

   if (lnum == 0) {   // got line one instead, correct oldLineInd
      oldLineInd = -1; // careful, it is negative!
   } else {
      oldLineInd = lnum - book->mem.lockedLow; // get line count before the insertion
   }
   lineCount = book->mem.lockedHigh - book->mem.lockedLow;

   DataBlock* block = (DataBlock *)(hdr->bh_data);

   //If
   //- there is not enough room in the current block
   //- appending to the last line in the block
   //- not appending to the last line in the file
   //then insert in front of the next block.
   if ((int)block->freeSpace < neededSpace && oldLineInd == lineCount - 1
      && lnum < book->mem.lineCount
   ) {
      // Now that the line is not going to be inserted in the block that we
      // expected, the line count has to be adjusted in the pointer blocks by using locked_lineadd
      --(book->mem.lockedInsertedLines);
      --(book->mem.lockedHigh);
      if ((hdr = ml_find_line(book, lnum + 1, ML_INSERT)) == NULL)
         goto theend;

      oldLineInd = -1;  // careful, it is negative!

      // get line count before the insertion
      lineCount = book->mem.lockedHigh - book->mem.lockedLow;
      CHECK(book->mem.lockedLow != lnum + 1, "lockedLow != lnum + 1");

      block = (DataBlock *)(hdr->bh_data);
   }

   ++book->mem.lineCount;
   int const newLineInd = oldLineInd + 1; 

   if ((int)block->freeSpace >= neededSpace) { // enough room in data block
      //Insert the new line in an existing data block, or in the data block allocated above.
      block->startByte -= len; // lines are added backwards, see DataBlock definition
      block->freeSpace -= neededSpace;
      ++(block->countLines);

      //shift the text of the lines that follow to the front to make space for the new line;
      //adjust the indices of those lines
      if (lineCount > newLineInd) { // if there are following lines
         // lineSentinel (the start of prv line) will become the character just after the new line
         int lineSentinel; 
         if (oldLineInd < 0) {
            lineSentinel = block->endByte;
         } else {
            lineSentinel = ((block->c[oldLineInd]) & c_MASK);
         }
         MEMMOVE(
            (char *)block + block->startByte,
            (char *)block + block->startByte + len,
            (Unt)(lineSentinel - (block->startByte + len))
         );
         for (i = lineCount - 1; i > oldLineInd; --i) {
            block->c[i + 1] = block->c[i] - len;
         }
         block->c[newLineInd] = lineSentinel - len;
      } else {
         // add line at the end of book (which is the start of the memory data structure)
         block->c[newLineInd] = block->startByte;
      }

      if (len > 1) {
         // copy the text into the block unless it's empty
         MEMMOVE((char *)block + block->c[newLineInd], newContent, (Unt)len);
      } else {
         *((char*)block + block->c[newLineInd]) = ZERO;
      }
      if (flags & ML_APPEND_MARK) {
         block->c[newLineInd] |= DB_MARKED;
      }

      // Mark the block dirty.
      book->mem.flags |= ML_LOCKED_DIRTY;
      if (!(flags & ML_APPEND_NEW))
         book->mem.flags |= ML_LOCKED_POS;
   } else {  //{{{ not enough space in data block
      long       lineCount_left, lineCount_right;
      int       pageCount_left, pageCount_right;
      BlockHeader* leftHeader;
      BlockHeader* rightHeader;
      BlockHeader* newBlock;
      int       lines_moved;
      int       data_moved = 0;       // init to shut up gcc
      int       total_moved = 0;       // init to shut up gcc
      DataBlock    *rightBlock, *leftBlock;
      int       stack_idx;
      int       in_left;
      int       lineadd;
      BlockId   bnum_left, bnum_right;
      LineNr    lnum_left, lnum_right;
      int       idx;
      PointerBlock       *pp_new;

      //There is not enough room, we have to create a new data block and copy some lines into it.
      //Then we have to insert an entry in the pointer block. If this pointer block also is full, 
      //we go up another block, and so on, up to the root if necessary.
      //The line counts in the pointer blocks have already been adjusted by ml_find_line().
      //
      //We are going to allocate a new data block. Depending on the situation it will be put to 
      //the left or right of the existing block. If possible we put the new line in the left block 
      //and move the lines after it to the right block. Otherwise the new line is
      //also put in the right block. This method is more efficient when
      //inserting a lot of lines at one place.
      if (oldLineInd < 0)   {// left block is new, right block is existing
          lines_moved = 0;
          in_left = true;
          // neededSpace does not change
       } else { // left block is existing, right block is new
         lines_moved = lineCount - oldLineInd - 1;
         if (lines_moved == 0)
            in_left = false;   // put new line in right block. neededSpace does not change
         else {
            data_moved = ((block->c[oldLineInd]) & c_MASK) - block->startByte;
            total_moved = data_moved + lines_moved * INDEX_SIZE;
            if ((int)block->freeSpace + total_moved >= neededSpace) {
               in_left = true;   // put new line in left block
               neededSpace = total_moved;
            } else {
               in_left = false;       // put new line in right block
               neededSpace += total_moved;
            }
         }
      }

      pageCount = ((neededSpace + HEADER_SIZE) + page_size - 1) / page_size;
      if ((newBlock = newDataBlock(mfp, flags & ML_APPEND_NEW, pageCount)) == NULL) {
         // correct line counts in pointer blocks
         --(book->mem.lockedInsertedLines);
         --(book->mem.lockedHigh);
         goto theend;
      }
      if (oldLineInd < 0) { // left block is new
         leftHeader = newBlock;
         rightHeader = hdr;
         lineCount_left = 0;
         lineCount_right = lineCount;
      } else { // right block is new
         leftHeader = hdr;
         rightHeader = newBlock;
         lineCount_left = lineCount;
         lineCount_right = 0;
      }
      rightBlock = (DataBlock *)(rightHeader->bh_data);
      leftBlock = (DataBlock *)(leftHeader->bh_data);
      bnum_left = leftHeader->bh_bnum;
      bnum_right = rightHeader->bh_bnum;
      pageCount_left = leftHeader->pageCount;
      pageCount_right = rightHeader->pageCount;

      // May move the new line into the right/new block.
      if (!in_left) {
         rightBlock->startByte -= len;
         rightBlock->freeSpace -= len + INDEX_SIZE;
         rightBlock->c[0] = rightBlock->startByte;
         if (flags & ML_APPEND_MARK)
            rightBlock->c[0] |= DB_MARKED;

         if (len > 0) {
            MEMMOVE((char *)rightBlock + rightBlock->startByte, newContent, (Unt)len);
         }
         ++lineCount_right;
      }
      // may move lines from the left/old block to the right/new one.
      if (lines_moved) {
         rightBlock->startByte -= data_moved;
         rightBlock->freeSpace -= total_moved;
         MEMMOVE((char *)rightBlock + rightBlock->startByte,
            (char *)leftBlock + leftBlock->startByte,
            (Unt)data_moved
         );
         offset = rightBlock->startByte - leftBlock->startByte;
         leftBlock->startByte += data_moved;
         leftBlock->freeSpace += total_moved;

         /*
          * update indexes in the new block
          */
         for (to = lineCount_right, from = newLineInd; from < lineCount_left; ++from, ++to) {
            rightBlock->c[to] = block->c[from] + offset;
         }
         lineCount_right += lines_moved;
         lineCount_left -= lines_moved;
      }

      // May move the new line into the left (old or new) block.
      if (in_left) {
          leftBlock->startByte -= len;
          leftBlock->freeSpace -= len + INDEX_SIZE;
          leftBlock->c[lineCount_left] = leftBlock->startByte;
          if (flags & ML_APPEND_MARK)
             leftBlock->c[lineCount_left] |= DB_MARKED;
          if (len > 0) {
             MEMMOVE((char *)leftBlock + leftBlock->startByte, newContent, (Unt)len);
          }
          ++lineCount_left;
      }

      if (oldLineInd < 0)    {// left block is new
         lnum_left = lnum + 1;
         lnum_right = 0;
      } else { // right block is new
         lnum_left = 0;
         if (in_left)
            lnum_right = lnum + 2;
         else
            lnum_right = lnum + 1;
      }
      leftBlock->countLines = lineCount_left;
      rightBlock->countLines = lineCount_right;

       // release the two data blocks. The new one (newBlock) already has a correct blocknumber.
       // The old one (hdr, in locked) gets a positive blocknumber if we changed it and we 
       // are not editing a new file.
      if (lines_moved || in_left)
         book->mem.flags |= ML_LOCKED_DIRTY;
      if ((flags & ML_APPEND_NEW) == 0 && oldLineInd >= 0 && in_left)
         book->mem.flags |= ML_LOCKED_POS;
      mf_put(mfp, newBlock, true, false);

      // flush the old data block. set lockedInsertedLines to 0, because the updating of the
      // pointer blocks is done below
      lineadd = book->mem.lockedInsertedLines;
      book->mem.lockedInsertedLines = 0;
      ml_find_line(book, (LineNr)0, ML_FLUSH);   // flush data block

      // update pointer blocks for the new data block
      for (stack_idx = book->mem.ml_stack_top - 1; stack_idx >= 0; --stack_idx) {
         ip = &(book->mem.ml_stack[stack_idx]);
         idx = ip->ip_index;
         if ((hdr = mf_get(mfp, ip->ip_bnum, 1)) == NULL)
            goto theend;
         pp = (PointerBlock *)(hdr->bh_data);   // must be pointer block
         if (pp->id != PTR_ID) {
            internalErrMsg(e_pointer_block_id_wrong_three);
            mf_put(mfp, hdr, false, false);
            goto theend;
         }
         //TODO: If the pointer block is full and we are adding at the end
         //try to insert in front of the next block
         // block not full, add one entry
         if (pp->pointerCount < pp->pointerCountMax) {
            if (idx + 1 < (int)pp->pointerCount) {
                MEMMOVE(&pp->c[idx + 2], &pp->c[idx + 1], (Unt)(pp->pointerCount - idx - 1) * sizeof(PtrEntry));
            }
            ++pp->pointerCount;
            pp->c[idx].lineCount = lineCount_left;
            pp->c[idx].blockId = bnum_left;
            pp->c[idx].pageCount = pageCount_left;
            pp->c[idx + 1].lineCount = lineCount_right;
            pp->c[idx + 1].blockId = bnum_right;
            pp->c[idx + 1].pageCount = pageCount_right;

            if (lnum_left != 0)
               pp->c[idx].oldLnum = lnum_left;
            if (lnum_right != 0)
               pp->c[idx + 1].oldLnum = lnum_right;

            mf_put(mfp, hdr, true, false);
            book->mem.ml_stack_top = stack_idx + 1;       // truncate stack

            if (lineadd) {
               --(book->mem.ml_stack_top);
               // fix line count for rest of blocks in the stack
               fixBlockStack(book, lineadd);
                    // fix stack itself
               book->mem.ml_stack[book->mem.ml_stack_top].ip_high += lineadd;
               ++(book->mem.ml_stack_top);
            }

            break;
         }
         // pointer block full
         //split the pointer block
         //allocate a new pointer block
         //move some of the pointer into the new block
         //prepare for updating the parent block
         for (;;) { // do this twice when splitting block 1
            newBlock = ml_new_ptr(mfp);
            if (newBlock == NULL)       // TODO: try to fix tree
               goto theend;
            pp_new = (PointerBlock *)(newBlock->bh_data);

            if (hdr->bh_bnum != 1)
               break;

            //if block 1 becomes full the tree is given an extra level The pointers from block 1 
            //are moved into the new block. block 1 is updated to point to the new block
            //then continue to split the new block
            MEMMOVE(pp_new, pp, (Unt)page_size);
            pp->pointerCount = 1;
            pp->c[0].blockId = newBlock->bh_bnum;
            pp->c[0].lineCount = book->mem.lineCount;
            pp->c[0].oldLnum = 1;
            pp->c[0].pageCount = 1;
            mf_put(mfp, hdr, true, false);   // release block 1
            hdr = newBlock;      // new block is to be split
            pp = pp_new;
            CHECK(stack_idx != 0, _("stack_idx should be 0"));
            ip->ip_index = 0;
            ++stack_idx;   // do block 1 again later
         }
         //move the pointers after the current one to the new block
         //If there are none, the new entry will be in the new block.
         total_moved = pp->pointerCount - idx - 1;
         if (total_moved) {
            MEMMOVE(
               &pp_new->c[0],
               &pp->c[idx + 1],
               (Unt)(total_moved) * sizeof(PtrEntry)
            );
            pp_new->pointerCount = total_moved;
            pp->pointerCount -= total_moved - 1;
            pp->c[idx + 1].blockId = bnum_right;
            pp->c[idx + 1].lineCount = lineCount_right;
            pp->c[idx + 1].pageCount = pageCount_right;
            if (lnum_right)
               pp->c[idx + 1].oldLnum = lnum_right;
         } else {
            pp_new->pointerCount = 1;
            pp_new->c[0].blockId = bnum_right;
            pp_new->c[0].lineCount = lineCount_right;
            pp_new->c[0].pageCount = pageCount_right;
            pp_new->c[0].oldLnum = lnum_right;
         }
         pp->c[idx].blockId = bnum_left;
         pp->c[idx].lineCount = lineCount_left;
         pp->c[idx].pageCount = pageCount_left;
         if (lnum_left)
            pp->c[idx].oldLnum = lnum_left;
         lnum_left = 0;
         lnum_right = 0;

         // recompute line counts
         lineCount_right = 0;
         for (i = 0; i < (int)pp_new->pointerCount; ++i)
            lineCount_right += pp_new->c[i].lineCount;
         lineCount_left = 0;
         for (i = 0; i < (int)pp->pointerCount; ++i)
            lineCount_left += pp->c[i].lineCount;

         bnum_left = hdr->bh_bnum;
         bnum_right = newBlock->bh_bnum;
         pageCount_left = 1;
         pageCount_right = 1;
         mf_put(mfp, hdr, true, false);
         mf_put(mfp, newBlock, true, false);
      }

      // Safety check: fallen out of for loop?
      if (stack_idx < 0) {
         internalErrMsg(e_updated_too_many_blocks);
         book->mem.ml_stack_top = 0;   // invalidate stack
      }
   }//}}}

   // The line was inserted below 'lnum'
   updateChunk(book, lnum + 1, (Long)textLen, ML_CHNK_ADDLINE);

   if (book->writeToChannel)
      channel_write_new_lines(book);
   ret = OK;

theend:
   eeglFree(tofree);
   return ret;
}

// Flush any pending change and call insertLineText()
private int
appendFlush(
   Book   *book,
   LineNr   lnum,      // append after this line (can be 0)
   Arr(Byte) newContent,   // text of the new line
   ColNr   len,      // length of line, including ZERO, or 0
   int      flags      // ML_APPEND_ flags
){
   if (lnum > book->mem.lineCount)
      return FAIL;  // lnum out of range

   if (book->mem.ml_line_lnum != 0)
      // This may also invoke insertLineText().
      flushLine(book);

   // When inserting above recorded changes: flush the changes before changing
   // the text.  Then flush the cached line, it may become invalid.
   may_doInvokeListenersOnChangedText(book, lnum + 1, lnum + 1, 1);
   if (book->mem.ml_line_lnum != 0)
      flushLine(book);
   return insertLineText(book, lnum, newContent, len, flags);
}

//Append a line to curBook after lnum (may be 0 to insert a line in front of the file).
//"line" does not need to be allocated, but can't be another line in a
//book, unlocking may make it invalid.
//
//"newfile": true when starting to edit a new file, meaning that oldLnum will be set for 
//recovery.
//
//Check: The caller of this function should probably also call appended_lines().
//return FAIL for failure, OK otherwise
pub int
ml_append(
   LineNr lnum, // append after this line (can be 0)
   CS newContent, // text of the new line
   ColNr len, // number of bytes to copy, or if 0 - will be replaced by strlen(newContent), 
   int   newfile // flag, see above
){
   if (len == 0) {
      if (newContent == NULL) {
         return ml_append_flags(lnum, newContent, 1, newfile ? ML_APPEND_NEW : 0);
      } else {
         return ml_append_flags(lnum, newContent, STRLEN(newContent) + 1, newfile ? ML_APPEND_NEW : 0);
      } 
   } else {
      return ml_append_flags(lnum, newContent, len + 1, newfile ? ML_APPEND_NEW : 0);
   }
}

pub int
ml_append_flags(
   LineNr   lnum,      // append after this line (can be 0)
   CS newContent,      // text of the new line
   ColNr   len,        // length of new line, including ZERO, or 0
   Unt      flags      // ML_APPEND_ values
){
   // When starting up, we might still need to create the memfile
   if (curBook->mem.mfile == NULL && bookOpenFromInvo(false, NULL, 0) == FAIL)
      return FAIL;
   return appendFlush(curBook, lnum, newContent, len, flags);
}


//Like ml_append() but for an arbitrary book. The buffer must already have a memline.
//"newfile": true when starting to edit a new file, meaning that oldLnum will be set for recovery.
pub int
memAppendBook(
   Book* book,
   LineNr lnum,  // append after this line (can be 0)
   CS line,      // text of the new line
   ColNr len,    // length of new line, including ZERO, or 0
   int newfile  // flag, see above
){
   if (book->mem.mfile == NULL)
      return FAIL;
   return appendFlush(book, lnum, line, len, newfile ? ML_APPEND_NEW : 0);
}

//Replace line "lnum", with buffering, in current book.
//
//If "copy" is true, make a copy of the line, otherwise the line has been copied to allocated 
//memory already. If "copy" is false the "line" may be freed to add text properties! Do not use 
//it after calling ml_replace().
//Check: The caller of this function should probably also call doChangedLines(), unless 
//drawUpdateScreen(UPD_NOT_VALID) is used.
//return FAIL for failure, OK otherwise
pub int
ml_replace(LineNr lnum, CS line, int copy) {
   ColNr len = -1;

   if (line)
      len = (ColNr)STRLEN(line);
   return ml_replace_len(lnum, line, len, false, copy);
}

//Replace a line for the current buffer. Like ml_replace() with: "len_arg" is the length of the 
//text, excluding ZERO. If "has_props" is true then "line_arg" includes the text properties 
//and "len_arg" includes the ZERO of the text and text properties. When "copy" is true copy 
//the text into allocated memory, otherwise "line_arg" must be allocated and will be consumed here.
pub int
ml_replace_len(
   LineNr lnum,
   CS line_arg,
   ColNr len_arg,
   int has_props,
   int copy
){
   CS line = line_arg;

   if (!line)      // just checking...
      return FAIL;

   ColNr len = len_arg;
   // When starting up, we might still need to create the memfile
   if (curBook->mem.mfile == NULL && bookOpenFromInvo(false, NULL, 0) == FAIL)
      return FAIL;

   if (!has_props)
      ++len;  // include the ZERO after the text
   if (copy) {
      // copy the line to allocated memory
      if (has_props)
         line = eeMemsave(line, len);
      else
         line = copySubstr(line, len - 1);
      if (!line)
         return FAIL;
   }

   if (curBook->mem.ml_line_lnum != lnum) {
      // another line is buffered, flush it
      flushLine(curBook);

      if (curBook->hasTextprop && !has_props)
         // Need to fetch the old line to copy over any text properties.
         memGetLine(curBook, lnum, true);
   }

   if (curBook->hasTextprop && !has_props) {
      Unt   oldtextlen = STRLEN(curBook->mem.cachedLine) + 1;

      if (oldtextlen < (Unt)curBook->mem.lineLen) {
         Byte *newline;
         Unt textproplen = curBook->mem.lineLen - oldtextlen;

         // Need to copy over text properties, stored after the text.
         newline = alloc(len + (int)textproplen);
         if (newline != NULL) {
            MEMMOVE(newline, line, len);
            MEMMOVE(newline + len, curBook->mem.cachedLine + oldtextlen, textproplen);
            eeglFree(line);
            line = newline;
            len += (ColNr)textproplen;
         }
      }
   }

   if (curBook->mem.flags & ML_LINE_DIRTY)
      eeglFree(curBook->mem.cachedLine);   // free allocated line

   curBook->mem.cachedLine = line;
   curBook->mem.lineLen = len;
   curBook->mem.lineTextLen = !has_props ? len_arg + 1 : 0;
   curBook->mem.ml_line_lnum = lnum;
   curBook->mem.flags = (curBook->mem.flags | ML_LINE_DIRTY) & ~ML_EMPTY;

   return OK;
}

//Adjust text properties in line "lnum" for a deleted line. When "above" is true this is the line 
//above the deleted line, otherwise this is the line below the deleted line.
//"del_props[del_props_len]" are the properties of the deleted line.
private void
adjustTextPropsForDeletion(
   Book* book,
   LineNr lnum,
   CS del_props,
   int del_props_len,
   int above
) {
   int      did_get_line = false;
   int      done_this;
   TextProp   prop_del;
   BlockHeader   *hdr;
   DataBlock* block;
   int idx;
   int line_start;
   long   line_size;
   int this_props_len = 0;
   CS text;
   Unt   textlen;
   int      found;

   for (int done_del = 0; done_del < del_props_len; done_del += sizeof(TextProp)) {
      MEMMOVE(&prop_del, del_props + done_del, sizeof(TextProp));
      if ((above && (prop_del.flags & TEXT_PROP_CONT_PREV)
             && !(prop_del.flags & TEXT_PROP_CONT_NEXT))
          || (!above && (prop_del.flags & TEXT_PROP_CONT_NEXT) 
             && !(prop_del.flags & TEXT_PROP_CONT_PREV))
      ){
         if (!did_get_line) {
            did_get_line = true;
            if ((hdr = ml_find_line(book, lnum, ML_FIND)) == NULL)
               return;

            block = (DataBlock *)(hdr->bh_data);
            idx = lnum - book->mem.lockedLow;
            line_start = ((block->c[idx]) & c_MASK);
            if (idx == 0)      // first line in block, text at the end
               line_size = block->endByte - line_start;
            else
               line_size = ((block->c[idx - 1]) & c_MASK) - line_start;
            text = (CS)block + line_start;
            textlen = STRLEN(text) + 1;
            if ((long)textlen >= line_size) {
               if (above)
                  internal_error(S"no text property above deleted line");
               else
                  internal_error(S"no text property below deleted line");
               return;
            }
            this_props_len = line_size - (int)textlen;
         }

         found = false;
         for (done_this = 0; done_this < this_props_len; done_this += sizeof(TextProp)) {
            int flag = above ? TEXT_PROP_CONT_NEXT : TEXT_PROP_CONT_PREV;
            TextProp  prop_this;

            MEMMOVE(&prop_this, text + textlen + done_this, sizeof(TextProp));
            if ((prop_this.flags & flag)
               && prop_del.id == prop_this.id
               && prop_del.type == prop_this.type
            ){
               found = true;
               prop_this.flags &= ~flag;
               MEMMOVE(text + textlen + done_this, &prop_this, sizeof(TextProp));
               break;
            }
         }
         if (!found) {
            if (above)
               internal_error(S"text property above deleted line not found");
            else
               internal_error(S"text property below deleted line not found");
         }

         book->mem.flags |= (ML_LOCKED_DIRTY | ML_LOCKED_POS);
      }
   }
}

// Delete line "lnum" in the current book.
// When "flags" has ML_DEL_MESSAGE may give a "No lines in book" message.
// When "flags" has ML_DEL_UNDO this is called from undo.
//
// return FAIL for failure, OK otherwise
private int
deleteLine(Book* book, LineNr lnum, int flags) {
   BlockHeader* hdr;
   DataBlock* block;
   PointerBlock* pp;
   InfoPtr* ip;
   int count;       // number of entries in block
   int idx;
   int stack_idx;
   long line_size;
   int i;
   int ret = FAIL;
   CS textprop_save = NULL;
   long textprop_len = 0;

   if (lowest_marked && lowest_marked > lnum)
      lowest_marked--;

   // If the file becomes empty the last line is replaced by an empty line.
   if (book->mem.lineCount == 1) {    // file becomes empty
       if ((flags & ML_DEL_MESSAGE)) {
          set_keep_msg((CS)_(no_lines_msg), 0);
       }

       i = ml_replace((LineNr)1, S"", true);
       book->mem.flags |= ML_EMPTY;

       return i;
   }

   // Find the data block containing the line.
   // This also fills the stack with the blocks from the root to the data block.
   // This also releases any locked block..
   MemFile* mfp = book->mem.mfile;
   if (!mfp)
      return FAIL;

   if ((hdr = ml_find_line(book, lnum, ML_DELETE)) == NULL)
      return FAIL;

   block = (DataBlock *)(hdr->bh_data);
   // compute line count before the delete
   count = (long)(book->mem.lockedHigh) - (long)(book->mem.lockedLow) + 2;
   idx = lnum - book->mem.lockedLow;

   --book->mem.lineCount;

   int line_start = ((block->c[idx]) & c_MASK);
   if (idx == 0)      // first line in block, text at the end
      line_size = block->endByte - line_start;
   else
      line_size = ((block->c[idx - 1]) & c_MASK) - line_start;

   // If there are text properties compute their byte length.
   // if needed make a copy, so that we can update properties in preceding and following lines.
   if (book->hasTextprop) {
      Unt   textlen = STRLEN((CS)block + line_start) + 1;
      textprop_len = line_size - (long)textlen;
      if (!(flags & (ML_DEL_UNDO | ML_DEL_NOPROP)) && textprop_len > 0) {
         textprop_save = eeMemsave((CS)block + line_start + textlen, textprop_len);
      } 
    }

   //special case: If there is only one line in the data block it becomes empty. Then we have to 
   //remove the entry, pointing to this data block, from the pointer block. If this pointer block 
   //also becomes empty, we go up another block, and so on, up to the root if necessary.
   //The line counts in the pointer blocks have already been adjusted by ml_find_line().
   if (count == 1) {
      mf_free(mfp, hdr);   // free the data block
      book->mem.locked = NULL;

      for (stack_idx = book->mem.ml_stack_top - 1; stack_idx >= 0; --stack_idx) {
         book->mem.ml_stack_top = 0;       // stack is invalid when failing
         ip = &(book->mem.ml_stack[stack_idx]);
         idx = ip->ip_index;
         if ((hdr = mf_get(mfp, ip->ip_bnum, 1)) == NULL)
            goto theend;
         pp = (PointerBlock *)(hdr->bh_data);   // must be pointer block
         if (pp->id != PTR_ID) {
            internalErrMsg(e_pointer_block_id_wrong_four);
            mf_put(mfp, hdr, false, false);
            goto theend;
         }
         count = --(pp->pointerCount);
         if (count == 0)       // the pointer block becomes empty!
            mf_free(mfp, hdr);
         else {
            if (count != idx)   // move entries after the deleted one
                MEMMOVE(&pp->c[idx], &pp->c[idx + 1], (Unt)(count - idx) * sizeof(PtrEntry));
            mf_put(mfp, hdr, true, false);

            book->mem.ml_stack_top = stack_idx;   // truncate stack
            // fix line count for rest of blocks in the stack
            if (book->mem.lockedInsertedLines != 0) {
                fixBlockStack(book, book->mem.lockedInsertedLines);
                book->mem.ml_stack[book->mem.ml_stack_top].ip_high += book->mem.lockedInsertedLines;
            }
            ++(book->mem.ml_stack_top);

            break;
         }
      }
      CHECK(stack_idx < 0, _("deleted block 1?"));
   } else {
      // delete the text by moving the next lines forwards
      int text_start = block->startByte;
      MEMMOVE(
         block + text_start + line_size, block + text_start, (Unt)(line_start - text_start)
      );

      //delete the index by moving the next indexes backwards
      //Adjust the indexes for the text movement.
      for (i = idx; i < count - 1; ++i)
         block->c[i] = block->c[i + 1] + line_size;

      block->freeSpace += line_size + INDEX_SIZE;
      block->startByte += line_size;
      --(block->countLines);

      // mark the block dirty and make sure it is in the file (for recovery)
      book->mem.flags |= (ML_LOCKED_DIRTY | ML_LOCKED_POS);
   }

   updateChunk(book, lnum, line_size - textprop_len, ML_CHNK_DELLINE);
   ret = OK;

theend:
   if (textprop_save) {
      // Adjust text properties in the line above and below.
      if (lnum > 1)
         adjustTextPropsForDeletion(book, lnum - 1, textprop_save, (int)textprop_len, true);
      if (lnum <= book->mem.lineCount) {
         adjustTextPropsForDeletion(book, lnum, textprop_save, (int)textprop_len, false);
      }
   }
   eeglFree(textprop_save);
   return ret;
}


// Delete line "lnum" in the current book.
// When "message" is true may give a "No lines in book" message.
//
// Check: The caller of this function should probably also call
// deleted_lines() after this.
//
// return FAIL for failure, OK otherwise
pub int
ml_delete(LineNr lnum) {
   return ml_delete_flags(lnum, 0);
}

// Delete line "lnum" in the current book. When "message" is true may give a 
// "No lines in book" message.
// Check: The caller of this function should probably also call deleted_lines() after this.
//
// return FAIL for failure, OK otherwise
pub int
ml_deleteBufLine(Book* book, LineNr lnum) {
   flushLine(book);
   if (lnum < 1 || lnum > book->mem.lineCount)
      return FAIL;

   // When inserting above recorded changes: flush the changes before changing the text.
   may_doInvokeListenersOnChangedText(book, lnum, lnum + 1, -1);

   return deleteLine(book, lnum, 0);
}

// Like ml_delete() but using flags (see deleteLine()).
pub int
ml_delete_flags(LineNr lnum, int flags) {
   flushLine(curBook);
   if (lnum < 1 || lnum > curBook->mem.lineCount)
      return FAIL;

   // When inserting above recorded changes: flush the changes before changing the text.
   may_doInvokeListenersOnChangedText(curBook, lnum, lnum + 1, -1);

   return deleteLine(curBook, lnum, flags);
}

// set the DB_MARKED flag for line 'lnum'
pub void
ml_setmarked(LineNr lnum) {
                // invalid line number
   if (lnum < 1 || lnum > curBook->mem.lineCount || curBook->mem.mfile == NULL)
      return;             // TODO give error message?

   if (lowest_marked == 0 || lowest_marked > lnum)
      lowest_marked = lnum;

   // find the data block containing the line This also fills the stack with the blocks from the 
   // root to the data block This also releases any locked block.
   BlockHeader* hdr;
   if ((hdr = ml_find_line(curBook, lnum, ML_FIND)) == NULL)
      return;          // TODO give error message?

   DataBlock* block = (DataBlock *)(hdr->bh_data);
   block->c[lnum - curBook->mem.lockedLow] |= DB_MARKED;
   curBook->mem.flags |= ML_LOCKED_DIRTY;
}

// find the first line with its DB_MARKED flag set
pub LineNr
ml_firstmarked(void) {

   if (curBook->mem.mfile == NULL)
      return (LineNr) 0;

   //The search starts with lowest_marked line. This is the last line where
   //a mark was found, adjusted by inserting/deleting lines.
   for (LineNr lnum = lowest_marked; lnum <= curBook->mem.lineCount; ) {
      //Find the data block containing the line.
      //This also fills the stack with the blocks from the root to the data
      //block This also releases any locked block.
      BlockHeader* hdr;
      if ((hdr = ml_find_line(curBook, lnum, ML_FIND)) == NULL)
         return (LineNr)0;          // give error message?

      DataBlock* block = (DataBlock *)(hdr->bh_data);

      for (int i = lnum - curBook->mem.lockedLow; lnum <= curBook->mem.lockedHigh; ++i, ++lnum) {
         if ((block->c[i]) & DB_MARKED) {
            (block->c[i]) &= c_MASK;
            curBook->mem.flags |= ML_LOCKED_DIRTY;
            lowest_marked = lnum + 1;
            return lnum;
         }
      } 
   }

   return (LineNr) 0;
}

// clear all DB_MARKED flags
pub void
ml_clearmarked(void) {
   if (curBook->mem.mfile == NULL)       // nothing to do
      return;

   //The search starts with line lowest_marked.
   for (LineNr lnum = lowest_marked; lnum <= curBook->mem.lineCount; ) {
      // Find the data block containing the line. This also fills the stack with the blocks from 
      // the root to the data block and releases any locked block.
      BlockHeader   *hdr;
      if ((hdr = ml_find_line(curBook, lnum, ML_FIND)) == NULL)
         return;      // give error message?

      DataBlock* block = (DataBlock *)(hdr->bh_data);

      for (int i = lnum - curBook->mem.lockedLow; lnum <= curBook->mem.lockedHigh; ++i, ++lnum) {
         if ((block->c[i]) & DB_MARKED) {
            (block->c[i]) &= c_MASK;
            curBook->mem.flags |= ML_LOCKED_DIRTY;
         }
      }
   }

   lowest_marked = 0;
}

// flush ml_line if necessary
private void
flushLine(Book *book) {
   BlockHeader   *hdr;
   DataBlock   *block;
   LineNr   lnum;
   CS new_line;
   CS old_line;
   ColNr   new_len;
   int      old_len;
   int      extra;
   int      idx;
   int      start;
   int      count;
   int      i;
   static int  entered = false;

   if (book->mem.ml_line_lnum == 0 || book->mem.mfile == NULL)
      return;      // nothing to do

   if (book->mem.flags & ML_LINE_DIRTY) {
      // This code doesn't work recursively, but Netbeans may call back here
      // when obtaining the cursor position.
      if (entered)
         return;
      entered = true;

      lnum = book->mem.ml_line_lnum;
      new_line = book->mem.cachedLine;

      hdr = ml_find_line(book, lnum, ML_FIND);
      if (hdr == NULL)
         internalErrFmtMsg(e_cannot_find_line_nr, lnum);
      else {
         block = (DataBlock *)(hdr->bh_data);
         idx = lnum - book->mem.lockedLow;
         start = ((block->c[idx]) & c_MASK);
         old_line = (CS)block + start;
         if (idx == 0)   // line is last in block
            old_len = block->endByte - start;
         else      // text of previous line follows
            old_len = (block->c[idx - 1] & c_MASK) - start;
         new_len = book->mem.lineLen;
         extra = new_len - old_len;       // negative if lines gets smaller

         // if new line fits in data block, replace directly
         if ((int)block->freeSpace >= extra) {
            int old_prop_len = 0;
            if (book->hasTextprop)
               old_prop_len = old_len - (int)STRLEN(old_line) - 1;
            // if the length changes and there are following lines
            count = book->mem.lockedHigh - book->mem.lockedLow + 1;
            if (extra != 0 && idx < count - 1) {
                // move text of following lines
                MEMMOVE((char *)block + block->startByte - extra,
                  (char *)block + block->startByte,
                  (Unt)(start - block->startByte));

               // adjust pointers of this and following lines
               for (i = idx + 1; i < count; ++i)
                  block->c[i] -= extra;
            }
            block->c[idx] -= extra;

            // adjust free space
            block->freeSpace -= extra;
            block->startByte -= extra;

            // copy new line into the data block
            MEMMOVE(old_line - extra, new_line, (Unt)new_len);
            book->mem.flags |= (ML_LOCKED_DIRTY | ML_LOCKED_POS);
            // The else case is already covered by the insert and delete
            if (book->hasTextprop) {
                // Do not count the size of any text properties.
                extra += old_prop_len;
                extra -= new_len - (int)STRLEN(new_line) - 1;
            }
            if (extra != 0)
               updateChunk(book, lnum, (Long)extra, ML_CHNK_UPDLINE);
         } else {
            //Cannot do it in one data block: Delete and append. Append first, because deleteLine()
            //cannot delete the last line in a book, which causes trouble for a book
            //that has only one line. Don't forget to copy the mark!
            // How about handling errors???
            (void)insertLineText(book, lnum, new_line, new_len,
                ((block->c[idx] & DB_MARKED) ? ML_APPEND_MARK : 0) | ML_APPEND_NOPROP
            );
            (void)deleteLine(book, lnum, ML_DEL_NOPROP);
         }
      }
      eeglFree(new_line);

      entered = false;
   }

   book->mem.flags &= ~ML_LINE_DIRTY;
   book->mem.ml_line_lnum = 0;
}

// create a new, empty, data block
private BlockHeader *
newDataBlock(MemFile *mfp, int negative, int pageCount) {
   BlockHeader   *hdr;
   if ((hdr = mf_new(mfp, negative, pageCount)) == NULL)
      return NULL;

   DataBlock* block = (DataBlock *)(hdr->bh_data);
   block->id = DATA_ID;
   block->endByte = pageCount * mfp->pageSize;
   block->startByte = block->endByte; 
   block->freeSpace = block->startByte - HEADER_SIZE;
   block->countLines = 0;

   return hdr;
}

// create a new, empty, pointer block
private BlockHeader *
ml_new_ptr(MemFile *mfp) {
   BlockHeader   *hdr;
   if ((hdr = mf_new(mfp, false, 1)) == NULL)
      return NULL;

   PointerBlock* pp = (PointerBlock *)(hdr->bh_data);
   pp->id = PTR_ID;
   pp->pointerCount = 0;
   pp->pointerCountMax = pointerCountMax(mfp);

   return hdr;
}

// Lookup line 'lnum' in a memline.
//
//   action: if ML_DELETE or ML_INSERT the line count is updated while searching
//        if ML_FLUSH only flush a locked block
//        if ML_FIND just find the line
//
// If the block was found it is locked and put in locked.
// The stack is updated to lead to the locked block. The ip_high field in
// the stack is updated to reflect the last line in the block AFTER the
// insert or delete, also if the pointer block has not been updated yet. But
// if locked != NULL lockedInsertedLines must be added to ip_high.
//
// return: NULL for failure, pointer to block header otherwise
private BlockHeader *
ml_find_line(Book *book, LineNr lnum, int action) {
   DataBlock   *block;
   PointerBlock   *pp;
   InfoPtr* ip;
   BlockHeader   *hdr;
   LineNr   t;
   BlockId   bnum, bnum2;
   int      dirty;
   LineNr   low, high;
   int      top;
   int      pageCount;
   int      idx;

   MemFile* mfp = book->mem.mfile;

   // If there is a locked block check if the wanted line is in it.
   // If not, flush and release the locked block.
   // Don't do this for ML_INSERT_SAME, because the stack need to be updated.
   // Don't do this for ML_FLUSH, because we want to flush the locked block.
   // Don't do this when 'swapfile' is reset, we want to load all the blocks.
   if (book->mem.locked) {
      if (ML_SIMPLE(action)
         && book->mem.lockedLow <= lnum
         && book->mem.lockedHigh >= lnum
         && !dontReleaseBlocksS
      ) {
         // remember to update pointer blocks and stack later
         if (action == ML_INSERT) {
            ++(book->mem.lockedInsertedLines);
            ++(book->mem.lockedHigh);
         } ei (action == ML_DELETE) {
            --(book->mem.lockedInsertedLines);
            --(book->mem.lockedHigh);
         }
         return (book->mem.locked);
      }

      mf_put(mfp, book->mem.locked, book->mem.flags & ML_LOCKED_DIRTY,
                      book->mem.flags & ML_LOCKED_POS
      );
      book->mem.locked = NULL;

      //If lines have been added or deleted in the locked block, need to
      //update the line count in pointer blocks.
      if (book->mem.lockedInsertedLines != 0)
          fixBlockStack(book, book->mem.lockedInsertedLines);
   }

   if (action == ML_FLUSH)       // nothing else to do
      return NULL;

   bnum = 1;             // start at the root of the tree
   pageCount = 1;
   low = 1;
   high = book->mem.lineCount;

   if (action == ML_FIND) {// first try stack entries
      for (top = book->mem.ml_stack_top - 1; top >= 0; --top) {
         ip = &(book->mem.ml_stack[top]);
         if (ip->ip_low <= lnum && ip->ip_high >= lnum) {
            bnum = ip->ip_bnum;
            low = ip->ip_low;
            high = ip->ip_high;
            book->mem.ml_stack_top = top;   // truncate stack at prev entry
            break;
         }
      }
      if (top < 0)
         book->mem.ml_stack_top = 0;      // not found, start at the root
   } else   // ML_DELETE or ML_INSERT
      book->mem.ml_stack_top = 0;   // start at the root

   // search downwards in the tree until a data block is found
   for (;;) {
      if ((hdr = mf_get(mfp, bnum, pageCount)) == NULL)
         goto error_noblock;

      // update high for insert/delete
      if (action == ML_INSERT)
         ++high;
      ei (action == ML_DELETE)
         --high;

      block = (DataBlock *)(hdr->bh_data);
      if (block->id == DATA_ID) {// data block
         book->mem.locked = hdr;
         book->mem.lockedLow = low;
         book->mem.lockedHigh = high;
         book->mem.lockedInsertedLines = 0;
         book->mem.flags &= ~(ML_LOCKED_DIRTY | ML_LOCKED_POS);
         return hdr;
      }

      pp = (PointerBlock *)(block);      // must be pointer block
      if (pp->id != PTR_ID) {
         internalErrMsg(e_pointer_block_id_wrong);
         goto error_block;
      }

      if ((top = ml_add_stack(book)) < 0)   // add new entry to stack
         goto error_block;
      ip = &(book->mem.ml_stack[top]);
      ip->ip_bnum = bnum;
      ip->ip_low = low;
      ip->ip_high = high;
      ip->ip_index = -1;      // index not known yet

      dirty = false;
      for (idx = 0; idx < (int)pp->pointerCount; ++idx) {
         t = pp->c[idx].lineCount;
         CHECK(t == 0, _("lineCount is zero"));
         if ((low += t) > lnum) {
            ip->ip_index = idx;
            bnum = pp->c[idx].blockId;
            pageCount = pp->c[idx].pageCount;
            high = low - 1;
            low -= t;

            // a negative block number may have been changed
            if (bnum < 0) {
               bnum2 = mf_trans_del(mfp, bnum);
               if (bnum != bnum2) {
                  bnum = bnum2;
                  pp->c[idx].blockId = bnum;
                  dirty = true;
               }
            }

            break;
         }
      }
      if (idx >= (int)pp->pointerCount) {    // past the end: something wrong!
         if (lnum > book->mem.lineCount) {
            internalErrFmtMsg(e_line_number_out_of_range_nr_past_the_end, lnum - book->mem.lineCount);
         } else {
            internalErrFmtMsg(e_line_count_wrong_in_block_nr, bnum);
         }
         goto error_block;
      }
      if (action == ML_DELETE) {
         pp->c[idx].lineCount--;
         dirty = true;
      } ei (action == ML_INSERT) {
         pp->c[idx].lineCount++;
         dirty = true;
      }
      mf_put(mfp, hdr, dirty, false);
   }

error_block:
   mf_put(mfp, hdr, false, false);
error_noblock:
   // If action is ML_DELETE or ML_INSERT we have to correct the tree for the 
   // incremented/decremented line counts, because there won't be a line inserted/deleted after all
   if (action == ML_DELETE)
      fixBlockStack(book, 1);
   ei (action == ML_INSERT)
      fixBlockStack(book, -1);
   book->mem.ml_stack_top = 0;
   return NULL;
}

// add an entry to the info pointer stack. return -1 for failure, number of the new entry otherwise
private int
ml_add_stack(Book* book) {
   int top = book->mem.ml_stack_top;

   // may have to increase the stack size
   if (top == book->mem.ml_stack_size) {
      CHECK(top > 0, _("Stack size increases")); // more than 5 levels???

      InfoPtr* newstack = ALLOC_MULT(InfoPtr, book->mem.ml_stack_size + STACK_INCR);
      if (top > 0)
         MEMMOVE(newstack, book->mem.ml_stack, (Unt)top * sizeof(InfoPtr));
      eeglFree(book->mem.ml_stack);
      book->mem.ml_stack = newstack;
      book->mem.ml_stack_size += STACK_INCR;
   }

   book->mem.ml_stack_top++;
   return top;
}

// Update the pointer blocks on the stack for inserted/deleted lines. The stack itself is also 
// updated.
//
// When an insert/delete line action fails, the line is not inserted/deleted, but the pointer 
// blocks have already been updated. That is fixed here by walking through the stack.
//
// Count is the number of lines added, negative if lines have been deleted.
private void
fixBlockStack(Book* book, int count) {
   InfoPtr* ip;
   PointerBlock   *pp;
   MemFile   *mfp = book->mem.mfile;
   BlockHeader   *hdr;

   for (int idx = book->mem.ml_stack_top - 1; idx >= 0; --idx) {
      ip = &(book->mem.ml_stack[idx]);
      if ((hdr = mf_get(mfp, ip->ip_bnum, 1)) == NULL)
         break;
      pp = (PointerBlock *)(hdr->bh_data);   // must be pointer block
      if (pp->id != PTR_ID) {
         mf_put(mfp, hdr, false, false);
         internalErrMsg(e_pointer_block_id_wrong_two);
         break;
      }
      pp->c[ip->ip_index].lineCount += count;
      ip->ip_high += count;
      mf_put(mfp, hdr, true, false);
   }
}

// Print the ATTENTION message: info about an existing swap file.
private void
attention_message(Book* book, CS swapName) {
   FileStat st;

   ++no_wait_return;
   (void)emsg(_(e_attention));
   msg_puts(_("\nFound a swap file by the name \""));
   msg_home_replace(swapName);
   msg_puts(S"\"\n");
   Tyme swap_mtime = swapfile_info(swapName);
   msg_puts(_("While opening file \""));
   msg_outtrans(book->currFileName);
   msg_puts(S"\"\n");
   if (stat((char *)book->currFileName, &st) == -1) {
      msg_puts(_("      CANNOT BE FOUND"));
   } else {
      msg_puts(_("             dated: "));
      msg_puts(get_ctime(st.st_mtime, true));
      if (swap_mtime != 0 && st.st_mtime > swap_mtime)
         msg_puts(_("      NEWER than swap file!\n"));
   }
   // Some of these messages are long to allow translation to other languages.
   msg_puts(_("\n(1) Another program may be editing the same file.  If this is the case,"
            "\n    be careful not to end up with two different instances of the same\n    "
            "file when making changes.  Quit, or continue with caution.\n")
   );
   msg_puts(_("(2) An edit session for this file crashed.\n"));
   msg_puts(_("    If this is the case, use \":recover\" or \"eegl -r "));
   msg_outtrans(book->currFileName);
   msg_puts(_("\"\n    to recover the changes (see \":help recovery\").\n"));
   msg_puts(_("    If you did this already, delete the swap file \""));
   msg_outtrans(swapName);
   msg_puts(_("\"\n    to avoid this message.\n"));
   commlineRowG = msgRowG;
   --no_wait_return;
}

//Trigger the SwapExists autocommands. Return a value for equivalent to do_dialog().
private SeaChoice 
do_swapexists(Book* book, CS fname) {
   set_EeglVar_string(VV_SWAPNAME, fname, -1);
   set_EeglVar_string(VV_SWAPCHOICE, NULL, -1);

   //Trigger SwapExists autocommands with <afile> set to the file being
   //edited.  Disallow changing directory here.
   ++allBookLock;
   applyAutocomms(EVENT_SWAPEXISTS, book->currFileName, NULL, false, NULL);
   --allBookLock;

   set_EeglVar_string(VV_SWAPNAME, NULL, -1);

   switch (*get_EeglVar_str(VV_SWAPCHOICE)) {
   case 'o': return SEA_CHOICE_READONLY;
   case 'e': return SEA_CHOICE_EDIT;
   case 'r': return SEA_CHOICE_RECOVER;
   case 'd': return SEA_CHOICE_DELETE;
   case 'q': return SEA_CHOICE_QUIT;
   case 'a': return SEA_CHOICE_ABORT;
   }

   return SEA_CHOICE_NONE;
}

//Find out what name to use for the swap file for book 'book'.
//
//Return the name in allocated memory or NULL. When out of memory, "dirp" is set to NULL.
//
//Note: If BASENAMELEN is not correct, you will get error messages for not being able to open the 
//swap or undo file. Note: May trigger SwapExists autocmd, pointers may change!
private CS
findSwapName(Book* book, CS old_fname) {   // don't give warning for this file name
   int n;
   CS buf_fname = book->currFileName;

   CS fname = fiBuildSwapOrUndoFname(book->fullFileName, false);

   if ((n = (int)STRLEN(fname)) == 0) {// sanity check
      EE_CLEAR(fname);
      goto endOfName;
   }
   //check if the swapfile already exists
   if (mch_getperm(fname) < 0) {// it does not exist
      //Extra security check: When a swap file is a symbolic link, this
      //is most likely a symlink attack.
      FileStat sb;
      if (lstat((char *)fname, &sb) < 0)
         goto endOfName;
   }

   //A file name equal to old_fname is OK to use.
   if (old_fname && fnamecmp(fname, old_fname) == 0)
      goto endOfName;

   //get here when file already exists
   if (fname[n - 2] == 'w' && fname[n - 1] == 'p') {// first try
      //Give an error message, unless recovering, no file name, we are viewing a help file or 
      //when the path of the file is different (happens when all .swp files are in one directory)
      if (!recoveryModeG && buf_fname && !(book->flags & (BF_DUMMY | BF_NO_SEA))){
         Block0   b0;
         int      differ = false;

         // Try to read block 0 from the swap file to get the original file name (and inode number)
         int fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
         if (fd >= 0) {
            if (fiReadEintr(fd, &b0, sizeof(b0)) == sizeof(b0)) {
               // If the swapfile has the same directory as the
               // buffer don't compare the directory names, they can have a different mountpoint.
               if (b0.b0_flags & B0_SAME_DIR) {
                  if (fnamecmp(fiGetShortFiName(book->fullFileName), fiGetShortFiName(b0.b0_fname)) != 0
                      || !same_directory(fname, book->fullFileName)
                  ) {
                     // Symlinks may point to the same file even
                     // when the name differs, need to check the inode too.
                     doExpandEnv(OUT nameBuffTextG, b0.b0_fname);
                     if (compareFnameWithInode(
                           book->fullFileName, nameBuffG, charToLong(b0.b0_ino)
                        )
                     )
                        differ = true;
                  }
               } else {
                  // The name in the swap file may be "~user/path/file".  Expand it first.
                  doExpandEnv(OUT nameBuffTextG, b0.b0_fname);
                  if (compareFnameWithInode(book->fullFileName, nameBuffG, charToLong(b0.b0_ino)))
                     differ = true;
               }
            }
            close(fd);
         }

         // give the ATTENTION message when there is an old swap file
         // for the current file, and the buffer was not recovered.
         if (differ == false && !(curBook->flags & BF_RECOVERED)) {
            SeaChoice choice = SEA_CHOICE_NONE;
            FileStat    st;
            process_still_running = false;
            // It's safe to delete the swap file if all these are true:
            // - the edited file exists
            // - the swap file has no changes and looks OK
            if (stat((char *)book->currFileName, &st) == 0 && swapfile_unchanged(fname)) {
               choice = SEA_CHOICE_DELETE;
               if (p_verbose > 0)
                  verb_msg(_("Found a swap file that is not useful, deleting it"));
            }

            // If there is an SwapExists autocommand and we can handle
            // the response, trigger it.  It may return 0 to ask the user anyway.
            if (choice == SEA_CHOICE_NONE
                && swap_exists_action != SEA_NONE
                && has_autocmd(EVENT_SWAPEXISTS, buf_fname, book))
            choice = do_swapexists(book, fname);

            if (choice == SEA_CHOICE_NONE && swap_exists_action == SEA_READONLY) {
               // always open readonly.
               choice = SEA_CHOICE_READONLY;
            }

            if (choice == SEA_CHOICE_NONE) {
               // Show info about the existing swap file.
               attention_message(book, fname);

               // We don't want a 'q' typed at the more-prompt interrupt loading a file.
               gotInterruptG = false;

               // If vimrc has "simalt ~x" we don't want it to interfere with the prompt here
               inpFlushBuffers(FLUSH_TYPEAHEAD);
            }

            if (swap_exists_action != SEA_NONE && choice == SEA_CHOICE_NONE) {
               Byte   *name;
               int   dialog_result;
               Unt  len = STRLEN(_("Swap file \""));

               name = alloc(STRLEN(fname) + len + STRLEN(_("\" already exists!")) + 5);
               if (!name) {
                  STRCPY(name, _("Swap file \""));
                  home_replace(fname, name + len, 1000, true);
                  STRCAT(name, _("\" already exists!"));
               }
               dialog_result = do_dialog(
                     EE_WARNING,
                     (CS)_("EEGL - ATTENTION"),
                     name == NULL ?  (CS)_("Swap file already exists!") : name,
                     process_still_running
                     ? (CS)_("&Open Read-Only\n&Edit anyway\n&Recover\n&Quit\n&Abort") :
                     (CS)_(
                        "&Open Read-Only\n&Edit anyway\n&Recover\n&Delete it\n&Quit\n&Abort"), 1, 
                        NULL, false
                     );

               if (process_still_running && dialog_result >= 4)
                  // compensate for missing "Delete it" button
                  dialog_result++;
               choice = dialog_result;
               eeglFree(name);

               // pretend screen didn't scroll, need redraw anyway
               msg_scrolled = 0;
               redraw_all_later(UPD_NOT_VALID);
            }

            switch (choice) {
            case SEA_CHOICE_READONLY:
               book->o.modifiable = false;
               break;
            case SEA_CHOICE_EDIT:
               break;
            case SEA_CHOICE_RECOVER:
               swap_exists_action = SEA_RECOVER;
               break;
            case SEA_CHOICE_DELETE:
               mch_remove(fname);
               break;
            case SEA_CHOICE_QUIT:
               swap_exists_action = SEA_QUIT;
               break;
            case SEA_CHOICE_ABORT:
               swap_exists_action = SEA_QUIT;
               gotInterruptG = true;
               break;
            case SEA_CHOICE_NONE:
               msg_puts(S"\n");
               if (msg_silent == 0)
                  // call wait_return() later
                  need_wait_return = true;
               break;
            }

            // If the file was deleted this fname can be used.
            if (choice != SEA_CHOICE_NONE && mch_getperm(fname) < 0)
               goto endOfName;

         }
      }
   }

   //Change the ".swp" extension to find another file that can be used.
   //First decrement the last char: ".swo", ".swn", etc.
   //If that still isn't enough decrement the last but one char: ".svz"
   //Can happen when editing many "No Name" buffers.
   if (fname[n - 1] == 'a') {// ".s?a"
      if (fname[n - 2] == 'a') {  // ".saa": tried enough, give up
         emsg(_(e_too_many_swap_files_found));
         EE_CLEAR(fname);
         goto endOfName;
      }
      --fname[n - 2];      // ".svz", ".suz", etc.
      fname[n - 1] = 'z' + 1;
   }
   --fname[n - 1];         // ".swo", ".swn", etc.
endOfName:
   return fname;
}

private int
b0_magic_wrong(Block0* b0p) {
    return (b0p->b0_magic_long != (long)B0_MAGIC_LONG
       || b0p->b0_magic_int != (int)B0_MAGIC_INT
       || b0p->b0_magic_short != (short)B0_MAGIC_SHORT
       || b0p->b0_magic_char != B0_MAGIC_CHAR);
}

//Compare current file name with file name from swap file.
//Try to use inode numbers when possible.
//Return non-zero when files are different.
//
//When comparing file names a few things have to be taken into consideration:
//- When working over a network the full path of a file depends on the host.
//  We check the inode number if possible.  It is not 100% reliable though,
//  because the device number cannot be used over a network.
//- When a file does not exist yet (editing a new file) there is no inode number.
//- The file name in a swap file may not be valid on the current host. The
//  "~user" form is used whenever possible to avoid this.
//
//This is getting complicated, let's make a table:
//
//     ino_c  ino_s  fname_c  fname_s   differ =
//
//both files exist -> compare inode numbers:
//     != 0   != 0   X    X   ino_c != ino_s
//
//inode number(s) unknown, file names available -> compare file names
//     == 0   X   OK    OK   fname_c != fname_s
//      X     == 0   OK    OK   fname_c != fname_s
//
//current file doesn't exist, file for swap file exist, file name(s) not
//available -> probably different
//     == 0   != 0    FAIL    X   true
//     == 0   != 0   X   FAIL   true
//
//current file exists, inode for swap unknown, file name(s) not
//available -> probably different
//     != 0   == 0    FAIL    X   true
//     != 0   == 0   X   FAIL   true
//
//current file doesn't exist, inode for swap unknown, one file name not
//available -> probably different
//     == 0   == 0    FAIL    OK   true
//     == 0   == 0   OK   FAIL   true
//
//current file doesn't exist, inode for swap unknown, both file names not
//available -> compare file names
//     == 0   == 0    FAIL   FAIL   fname_c != fname_s
//
//Note that when the ino_t is 64 bits, only the last 32 will be used.  This
//can't be changed without making the block 0 incompatible with 32 bit versions.
private int
compareFnameWithInode(CS fname_c, CS nameFromSwap, long ino_block0){
   FileStat st;
   ino_t ino_c = 0;      //ino of current file
   Byte buf_c[MAXPATHL]; //full path of fname_c
   Byte buf_s[MAXPATHL]; //full path of nameFromSwap

   if (stat((char *)fname_c, &st) == 0)
      ino_c = (ino_t)st.st_ino;

   //First we try to get the inode from the file name, because the inode in
   //the swap file may be outdated.  If that fails (e.g. this path is not
   //valid on this machine), use the inode from block 0. ino of file from swap file
   ino_t ino_s = ((stat((char *)nameFromSwap, &st) == 0) ? (ino_t)st.st_ino : (ino_t)ino_block0);

   if (ino_c && ino_s)
      return (ino_c != ino_s);

   //One of the inode numbers is unknown, try a forced eeFullFileName() and compare the file names
   int retval_c = eeFullFileName(fname_c, buf_c, MAXPATHL, true); //flag: buf_c valid
   int retval_s = eeFullFileName(nameFromSwap, buf_s, MAXPATHL, true); //flag: buf_s valid
   if (retval_c == OK && retval_s == OK)
      return STRCMP(buf_c, buf_s) != 0;

   //Can't compare inodes or file names, guess that the files are different, unless both appear 
   //not to exist at all, then compare with the file name in the swap file.
   if (ino_s == 0 && ino_c == 0 && retval_c == FAIL && retval_s == FAIL)
      return STRCMP(fname_c, nameFromSwap) != 0;
   return true;
}

//Move a long integer into a four byte character array. Used for machine independency in block zero
private void
longToChar(long n, CS s) {
   s[0] = (Byte)(n & 0xff);
   n = (unsigned)n >> 8;
   s[1] = (Byte)(n & 0xff);
   n = (unsigned)n >> 8;
   s[2] = (Byte)(n & 0xff);
   n = (unsigned)n >> 8;
   s[3] = (Byte)(n & 0xff);
}

private long
charToLong(CS s) {
   long retval = s[3];
   retval <<= 8;
   retval |= s[2];
   retval <<= 8;
   retval |= s[1];
   retval <<= 8;
   retval |= s[0];

   return retval;
}

//Set the flags in the first block of the swap file: file is modified or not: book->wasModified
pub void
ml_setflags(Book* book) {
   BlockHeader* hdr;
   Block0* b0p;

   if (!book->mem.mfile)
      return;
   for (hdr = book->mem.mfile->usedLast; hdr != NULL; hdr = hdr->bh_prev) {
      if (hdr->bh_bnum == 0) {
         b0p = (Block0 *)(hdr->bh_data);
         b0p->b0_dirty = book->wasModified ? B0_DIRTY : 0;
         hdr->bh_flags |= BH_DIRTY;
         mf_sync(book->mem.mfile, MFS_ZERO);
         break;
      }
   }
}

#define MLCS_MAXL 800   // max no of lines in chunk
#define MLCS_MINL 400   // should be half of MLCS_MAXL

//Keep information for finding byte offset of a line, updtype may be one of:
//ML_CHNK_ADDLINE: Add len to parent chunk, possibly splitting it
//     Careful: ML_CHNK_ADDLINE may cause ml_find_line() to be called.
//ML_CHNK_DELLINE: Subtract len from parent chunk, possibly deleting it
//ML_CHNK_UPDLINE: Add len to parent chunk, as a signed entity.
private void
updateChunk(Book* book, LineNr line, Long len, int updtype){
   static Book* ml_upd_lastbuf = NULL;
   static LineNr ml_upd_lastline;
   static LineNr ml_upd_lastcurline;
   static int ml_upd_lastcurix;

   LineNr curline = ml_upd_lastcurline;
   int curix = ml_upd_lastcurix;
   long size;
   MemChunkSize* curchnk;
   int rest;
   BlockHeader* hdr;
   DataBlock* block;

   if (book->mem.ml_usedchunks == -1 || len == 0)
      return;
   if (book->mem.ml_chunksize == NULL) {
      book->mem.ml_chunksize = ALLOC_MULT(MemChunkSize, 100);
      book->mem.ml_numchunks = 100;
      book->mem.ml_usedchunks = 1;
      book->mem.ml_chunksize[0].mlcs_numlines = 1;
      book->mem.ml_chunksize[0].mlcs_totalsize = 1;
   }

   if (updtype == ML_CHNK_UPDLINE && book->mem.lineCount == 1) {
      //First line in empty buffer from flushLine() -- reset
      book->mem.ml_usedchunks = 1;
      book->mem.ml_chunksize[0].mlcs_numlines = 1;
      book->mem.ml_chunksize[0].mlcs_totalsize = (long)book->mem.lineLen;
      return;
   }

   //Find chunk that our line belongs to, curline will be at start of the chunk.
   if (book != ml_upd_lastbuf || line != ml_upd_lastline + 1 || updtype != ML_CHNK_ADDLINE) {
      for (curline = 1, curix = 0;
           curix < book->mem.ml_usedchunks - 1
              && line >= curline + book->mem.ml_chunksize[curix].mlcs_numlines;
           curix++
      ) {
         curline += book->mem.ml_chunksize[curix].mlcs_numlines;
      }
   } ei (curix < book->mem.ml_usedchunks - 1
         && line >= curline + book->mem.ml_chunksize[curix].mlcs_numlines
   ) {
      // Adjust cached curix & curline
      curline += book->mem.ml_chunksize[curix].mlcs_numlines;
      curix++;
   }
   curchnk = book->mem.ml_chunksize + curix;

   if (updtype == ML_CHNK_DELLINE)
      len = -len;
   curchnk->mlcs_totalsize += len;
   if (updtype == ML_CHNK_ADDLINE) {
      curchnk->mlcs_numlines++;

      // May resize here so we don't have to do it in both cases below
      if (book->mem.ml_usedchunks + 1 >= book->mem.ml_numchunks) {
         MemChunkSize* t_chunksize = book->mem.ml_chunksize;

         book->mem.ml_numchunks = book->mem.ml_numchunks * 3 / 2;
         book->mem.ml_chunksize = eeRealloc(book->mem.ml_chunksize,
                sizeof(MemChunkSize) * book->mem.ml_numchunks);
         if (!book->mem.ml_chunksize) {
            // Hmmmm, Give up on offset for this buffer
            eeglFree(t_chunksize);
            book->mem.ml_usedchunks = -1;
            return;
         }
      }

      if (book->mem.ml_chunksize[curix].mlcs_numlines >= MLCS_MAXL) {
         int count;       // number of entries in block
         int idx;
         int end_idx;
         int textEnd;

         MEMMOVE(book->mem.ml_chunksize + curix + 1,
            book->mem.ml_chunksize + curix,
            (book->mem.ml_usedchunks - curix) *
            sizeof(MemChunkSize)
         );
         // Compute length of first half of lines in the split chunk
         size = 0;
         int linecnt = 0;
         while (curline < book->mem.lineCount && linecnt < MLCS_MINL) {
            if ((hdr = ml_find_line(book, curline, ML_FIND)) == NULL) {
               book->mem.ml_usedchunks = -1;
               return;
            }
            block = (DataBlock *)(hdr->bh_data);
            count = (long)(book->mem.lockedHigh) - (long)(book->mem.lockedLow) + 1;
            idx = curline - book->mem.lockedLow;
            curline = book->mem.lockedHigh + 1;

            // compute index of last line to use in this MEMLINE
            rest = count - idx;
            if (linecnt + rest > MLCS_MINL) {
               end_idx = idx + MLCS_MINL - linecnt - 1;
               linecnt = MLCS_MINL;
            } else {
               end_idx = count - 1;
               linecnt += rest;
            }
            if (book->hasTextprop) {

               //We cannot use the text pointers to get the text length,
               //the text prop info would also be counted.  Go over the lines.
               for (int i = end_idx; i < idx; ++i)
                  size += (int)STRLEN((CS)block + (block->c[i] & c_MASK)) + 1;
            } else {
               if (idx == 0) // first line in block, text at the end
                  textEnd = block->endByte;
               else
                  textEnd = ((block->c[idx - 1]) & c_MASK);
               size += textEnd - ((block->c[end_idx]) & c_MASK);
            }
         }
         book->mem.ml_chunksize[curix].mlcs_numlines = linecnt;
         book->mem.ml_chunksize[curix + 1].mlcs_numlines -= linecnt;
         book->mem.ml_chunksize[curix].mlcs_totalsize = size;
         book->mem.ml_chunksize[curix + 1].mlcs_totalsize -= size;
         book->mem.ml_usedchunks++;
         ml_upd_lastbuf = NULL;   // Force recalc of curix & curline
         return;
      } ei (book->mem.ml_chunksize[curix].mlcs_numlines >= MLCS_MINL
              && curix == book->mem.ml_usedchunks - 1
              && book->mem.lineCount - line <= 1
      ) {
         //We are in the last chunk and it is cheap to create a new one
         //after this. Do it now to avoid the loop above later on
         curchnk = book->mem.ml_chunksize + curix + 1;
         book->mem.ml_usedchunks++;
         if (line == book->mem.lineCount) {
            curchnk->mlcs_numlines = 0;
            curchnk->mlcs_totalsize = 0;
         } else {
            //Line is just prior to last, move count for last
            //This is the common case  when loading a new file
            hdr = ml_find_line(book, book->mem.lineCount, ML_FIND);
            if (hdr == NULL) {
               book->mem.ml_usedchunks = -1;
               return;
            }
            block = (DataBlock *)(hdr->bh_data);
            if (block->countLines == 1)
               rest = block->endByte - block->startByte;
            else
               rest = ((block->c[block->countLines - 2]) & c_MASK) - block->startByte;
            curchnk->mlcs_totalsize = rest;
            curchnk->mlcs_numlines = 1;
            curchnk[-1].mlcs_totalsize -= rest;
            curchnk[-1].mlcs_numlines -= 1;
          }
      }
   } ei (updtype == ML_CHNK_DELLINE) {
      curchnk->mlcs_numlines--;
      ml_upd_lastbuf = NULL;   // Force recalc of curix & curline
      if (curix < book->mem.ml_usedchunks - 1
         && curchnk->mlcs_numlines + curchnk[1].mlcs_numlines <= MLCS_MINL
      ) {
         curix++;
         curchnk = book->mem.ml_chunksize + curix;
      } ei (curix == 0 && curchnk->mlcs_numlines <= 0) {
         book->mem.ml_usedchunks--;
         MEMMOVE(
            book->mem.ml_chunksize, book->mem.ml_chunksize + 1,
            book->mem.ml_usedchunks * sizeof(MemChunkSize)
         );
         return;
      } ei (curix == 0 || (curchnk->mlcs_numlines > 10
             && curchnk->mlcs_numlines + curchnk[-1].mlcs_numlines > MLCS_MINL)
      ){
         return;
      }

      // Collapse chunks
      curchnk[-1].mlcs_numlines += curchnk->mlcs_numlines;
      curchnk[-1].mlcs_totalsize += curchnk->mlcs_totalsize;
      book->mem.ml_usedchunks--;
      if (curix < book->mem.ml_usedchunks)
          MEMMOVE(book->mem.ml_chunksize + curix,
            book->mem.ml_chunksize + curix + 1,
            (book->mem.ml_usedchunks - curix) *
            sizeof(MemChunkSize));
      return;
   }
   ml_upd_lastbuf = book;
   ml_upd_lastline = line;
   ml_upd_lastcurline = curline;
   ml_upd_lastcurix = curix;
}

//Find offset for line or line with offset.
//Find line with offset if "lnum" is 0; return remaining offset in offp
//Find offset of line if "lnum" > 0. Return -1 if information is not available
pub long
ml_find_line_or_offset(Book* book, LineNr lnum, long *offp) {
   BlockHeader* hdr;
   DataBlock* block;
   int count;      // number of entries in block
   int idx;
   int start_idx;
   int textEnd;
   int len;
   int extra = 0;

   // take care of cached line first
   flushLine(curBook);

   if (book->mem.ml_usedchunks == -1 || book->mem.ml_chunksize == NULL || lnum < 0)
      return -1;

   long offset = offp ? *offp : 0;
   if (lnum == 0 && offset <= 0)
      return 1;   // Not a "find offset" and offset 0 _must_ be in line 1
   //Find the last chunk before the one containing our line. Last chunk is
   //special because it will never qualify.
   LineNr curline = 1;
   long size = 0;
   int curix = 0;
   while (curix < book->mem.ml_usedchunks - 1
       && ((lnum != 0
           && lnum >= curline + book->mem.ml_chunksize[curix].mlcs_numlines)
            || (offset != 0 && offset > size + book->mem.ml_chunksize[curix].mlcs_totalsize))
   ) {
      curline += book->mem.ml_chunksize[curix].mlcs_numlines;
      size += book->mem.ml_chunksize[curix].mlcs_totalsize;
      curix++;
   }

   while ((lnum != 0 && curline < lnum) || (offset != 0 && size < offset)) {
      Unt textprop_total = 0;

      if (curline > book->mem.lineCount || (hdr = ml_find_line(book, curline, ML_FIND)) == NULL)
         return -1;
      block = (DataBlock *)(hdr->bh_data);
      count = (long)(book->mem.lockedHigh) - (long)(book->mem.lockedLow) + 1;
      idx = curline - book->mem.lockedLow;
      start_idx = idx;
      if (idx == 0)  // first line in block, text at the end
         textEnd = block->endByte;
      else
         textEnd = ((block->c[idx - 1]) & c_MASK);
      // Compute index of last line to use in this MEMLINE
      if (lnum != 0) {
         if (curline + (count - idx) >= lnum)
            idx += lnum - curline - 1;
         else
            idx = count - 1;
      } else {
         extra = 0;
         for (;;) {
            Unt textprop_size = 0;

            if (book->hasTextprop) {
                // compensate for the extra bytes taken by textprops
                Byte* l1 = (CS)block + ((block->c[idx]) & c_MASK);
                Byte* l2 = (CS)block 
                   + (idx == 0 ? block->endByte : ((block->c[idx - 1]) & c_MASK));
                textprop_size = (l2 - l1) - (STRLEN(l1) + 1);
            }
            if (!(offset >= size
                  + textEnd - (int)((block->c[idx]) & c_MASK)
                  - (long)(textprop_total + textprop_size)
                  )
            )
               break;

            textprop_total += textprop_size;
            if (idx == count - 1) {
               extra = 1;
               break;
            }
            idx++;
         }
      }
      if (book->hasTextprop && lnum != 0) {
         // cannot use the c pointer, need to get the actual text lengths.
         len = 0;
         for (int i = start_idx; i <= idx; ++i) {
            CS p = (CS)block + ((block->c[i]) & c_MASK);
            len += (int)STRLEN(p) + 1;
         }
      } else {
         len = textEnd - ((block->c[idx]) & c_MASK) - (long)textprop_total;
      }
      size += len;
      if (offset != 0 && size >= offset) {
         if (size == offset) {
            *offp = 0;
         } ei (idx == start_idx) {
            *offp = offset - size + len;
         } else {
            *offp = offset - size + len - (textEnd - ((block->c[idx - 1]) & c_MASK)) 
                    + (long)textprop_total;
         }
         curline += idx - start_idx + extra;
         if (curline > book->mem.lineCount)
            return -1;   // exactly one byte beyond the end
         return curline;
      }
      curline = book->mem.lockedHigh + 1;
   }

   if (lnum != 0) {
      // Don't count the last line break if 'noeol'
      if (lnum > book->mem.lineCount)
         size--;
   }

   return size;
}

// Give information about an existing swap file. Return timestamp (0 when unknown).
private Tyme
swapfile_info(CS fname) {
   FileStat       st;
   Byte uname[B0_UNAME_SIZE];

   // print the swap file date
   if (stat((char *)fname, &st) != -1) {
      // print name of owner of the file
      if (mch_get_uname(st.st_uid, uname, B0_UNAME_SIZE) == OK) {
         msg_puts(_("          owned by: "));
         msg_outtrans(uname);
         msg_puts(_("   dated: "));
      } else
         msg_puts(_("             dated: "));
      msg_puts(get_ctime(st.st_mtime, true));
   } else
      st.st_mtime = 0;

   // print the original file name
   int fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
   Block0   b0;
   if (fd >= 0) {
      if (fiReadEintr(fd, &b0, sizeof(b0)) == sizeof(b0)) {
         if (STRNCMP(b0.b0_version, "EEGL 3.0", 7) == 0) {
            msg_puts(_("         [from Eegl version 3.0]"));
         } ei (ml_check_b0_id(&b0) == FAIL) {
            msg_puts(_("         [does not look like an Eegl swap file]"));
         } else {
            msg_puts(_("         file name: "));
            if (b0.b0_fname[0] == ZERO)
               msg_puts(_("[No Name]"));
            else
               msg_outtrans(b0.b0_fname);

            msg_puts(_("\n          modified: "));
            msg_puts(b0.b0_dirty ? _("YES") : _("no"));

            if (*(b0.b0_uname) != ZERO) {
               msg_puts(_("\n         user name: "));
               msg_outtrans(b0.b0_uname);
            }

            if (*(b0.b0_hname) != ZERO) {
               if (*(b0.b0_uname) != ZERO)
                  msg_puts(_("   host name: "));
               else
                  msg_puts(_("\n         host name: "));
               msg_outtrans(b0.b0_hname);
            }

            if (charToLong(b0.b0_pid) != 0L) {
               msg_puts(_("\n        process ID: "));
               msg_outnum(charToLong(b0.b0_pid));
               if (swapfile_process_running(&b0, fname)) {
                  msg_puts(_(" (STILL RUNNING)"));
                  process_still_running = true;
               }
            }

            if (b0_magic_wrong(&b0)) {
               msg_puts(_("\n         [not usable on this computer]"));
            }
         }
      } else
          msg_puts(_("         [cannot be read]"));
      close(fd);
   } else
      msg_puts(_("         [cannot be opened]"));
   msg_putchar('\n');

   return st.st_mtime;
}

// Go to byte in book with offset 'cnt'.
pub void
goto_byte(long cnt) {
   long   boff = cnt;

   flushLine(curBook);   // cached line may be dirty
   setpcmark();
   if (boff)
      --boff;
   LineNr lnum = ml_find_line_or_offset(curBook, (LineNr)0, &boff);
   if (lnum < 1) {// past the end
      curPor->cursor.lnum = curBook->mem.lineCount;
      curPor->cursWant = MAXCOL;
      coladvance((ColNr)MAXCOL);
   } else {
      curPor->cursor.lnum = lnum;
      curPor->cursor.col = (ColNr)boff;
      curPor->cursor.coladd = 0;
      curPor->setCursWant = true;
   }
   check_cursor();

   // Make sure the cursor is on the first byte of a multi-byte char.
   mb_adjust_cursor();
}

//Append the full path to name with path separators made into percent
//signs, to "dir". An unnamed book is handled as "" (<currentdir>/"")
//The last character in "dir" must be an extra slash or backslash, it is removed.
pub CS
memMakePercentSwapName(CS dir, CS dir_end, CS name) {
   CS d = NULL;
   CS f = fiExpandAndCopy(name ? name : S"", true);
   CS s = alloc(STRLEN(f) + 1);
   STRCPY(s, f);
   for (d = s; *d != ZERO; MB_PTR_ADV(d)) {
      if (*d == '/')
         *d = '%';
   } 

   dir_end[-1] = ZERO;  // remove one trailing slash
   d = concat_fnames(dir, s, true);
   eeglFree(s);
   eeglFree(f);
   return d;
}

//}}}
//{{{memfile

//Contains the functions for handling blocks of memory which can
//be stored in a file. This is the implementation of a sort of virtual memory.
//
//A memfile consists of a sequence of blocks. The blocks numbered from 0
//upwards have been assigned a place in the actual file. The block number
//is equal to the page number in the file. The blocks with negative numbers are currently in 
//memory only. They can be assigned a place in the file when too much memory is being used. At that
//moment they get a new, positive, number. A list is used for translation of
//negative to positive numbers.
//
//The size of a block is a multiple of a page size, normally the page size of
//the device the file is on. Most blocks are 1 page long. A Block of multiple
//pages is used for a line that does not fit in a single page.
//
//Each block can be in memory and/or in a file. The block stays in memory as long as it is locked.
//If it is no longer locked it can be swapped out to the file. It is only written to the file if 
//it has been changed.
//
//Under normal operation the file is created when opening the memory file and
//deleted when closing the memory file. Only with recovery an existing memory file is opened.

//mch_open_rw(): invoke mch_open() with third argument for user R/W. open in rw------- mode
#define openRw(n, f)  open((n), (f), (mode_t)0600)

//Some systems have the page size in statfs.f_bsize, some in stat.st_blksize.
//TODO In Linux, stat.st_blksize is the preferred I/O block size for efficient file system 
//operations (typically 4096 bytes), while statfs.f_bsize represents the fundamental filesystem 
//block size used for free space calculation. Neither is guaranteed to be the hardware page size, 
//which is best obtained via sysconf(_SC_PAGE_SIZE)
#define STATFS stat
#define F_BSIZE st_blksize
#define fstatfs(fd, buf, len, ZERO) fstat((fd), (buf))

#define MEMFILE_PAGE_SIZE 4096      // default page size

private Ulong   total_mem_used = 0;   // total memory used for memfiles

private void mf_ins_hash(MemFile *, BlockHeader *);
private void mf_rem_hash(MemFile *, BlockHeader *);
private BlockHeader* mf_find_hash(MemFile *, BlockId);
private void mf_ins_used(MemFile *, BlockHeader *);
private void mf_rem_used(MemFile *, BlockHeader *);
private BlockHeader* mf_release(MemFile *, int);
private BlockHeader* mf_alloc_bhdr(MemFile *, int);
private void mf_free_bhdr(BlockHeader *);
private void mf_ins_free(MemFile *, BlockHeader *);
private BlockHeader* mf_rem_free(MemFile *);
private int mf_read(MemFile*, BlockHeader *);
private int mf_write(MemFile*, BlockHeader *);
private int mf_write_block(MemFile* mfp, BlockHeader *hp, FileOffset offset, unsigned size);
private int mf_trans_add(MemFile*, BlockHeader *);
private void mf_do_open(MemFile*, CS, Unt);
private void mf_hash_init(MfHashTable*);
private void mf_hash_free(MfHashTable*);
private void mf_hash_free_all(MfHashTable*);
private MfHashItem *mf_hash_find(MfHashTable*, BlockId);
private void mf_hash_add_item(MfHashTable*, MfHashItem*);
private void mf_hash_rem_item(MfHashTable*, MfHashItem*);
private int mf_hash_grow(MfHashTable*);

// The functions for using a memfile:
//
// mf_open()        open a new or existing memfile
// mf_open_file()   open a swap file for an existing memfile
// mf_close()       close (and delete) a memfile
// mf_new()         create a new block in a memfile and lock it
// mf_get()         get an existing block and lock it
// mf_put()         unlock a block, may be marked for writing
// mf_free()        remove a block
// mf_sync()        sync changed parts of memfile to disk
// mf_release_all() release as much memory as possible
// mf_trans_del()   may translate negative to positive block number
// mf_fullname()    make file name full path (use before first :cd)

//Open an existing or new memory block file.
//
// fname:   name of file to use (NULL means no file at all)
//    Note: fname must have been allocated, it is not copied!
//          If opening the file fails, fname is freed.
// flags:   flags for open() call
//
// If fname != NULL and file cannot be opened, fail.
//
//return value: identifier for this memory block file.
pub MemFile *
mf_open(CS fname, Unt flags) {
   FileOffset      size;
#if defined(STATFS) && !defined(__minix)
# define USE_FSTATFS
   struct STATFS   stf;
#endif

   MemFile* mfp = ALLOC_ONE(MemFile);

   if (!fname) {      // no file for this memfile, use memory only
      mfp->fName = NULL;
      mfp->fullFName = NULL;
      mfp->fd = -1;
   } else {
      mf_do_open(mfp, fname, flags);   // try to open the file

      // if the file cannot be opened, return here
      if (mfp->fd < 0) {
         eeglFree(mfp);
         return NULL;
      }
   }

   mfp->freeFirst = NULL;      // free list is empty
   mfp->usedFirst = NULL;      // used list is empty
   mfp->usedLast = NULL;
   mfp->mf_dirty = MF_DIRTY_NO;
   mfp->mf_used_count = 0;
   mf_hash_init(&mfp->mf_hash);
   mf_hash_init(&mfp->mf_trans);
   mfp->pageSize = MEMFILE_PAGE_SIZE;

#ifdef USE_FSTATFS
   //Try to set the page size equal to the block size of the device. Speeds up I/O a lot.
   //When recovering, the actual block size will be retrieved from block 0
   //in ml_recover(). The size used here may be wrong, therefore
   //mf_blocknr_max must be rounded up.
   if (mfp->fd >= 0
       && fstatfs(mfp->fd, &stf, sizeof(struct statfs), 0) == 0
       && stf.F_BSIZE >= MIN_SWAP_PAGE_SIZE
       && stf.F_BSIZE <= MAX_SWAP_PAGE_SIZE)
   mfp->pageSize = stf.F_BSIZE;
#endif

   if (mfp->fd < 0 || (flags & (O_TRUNC|O_EXCL))
        || (size = lseek(mfp->fd, (FileOffset)0L, SEEK_END)) <= 0)
      mfp->mf_blocknr_max = 0;   // no file or empty file
   else
      mfp->mf_blocknr_max = (BlockId)((size + mfp->pageSize - 1)
                      / mfp->pageSize);
   mfp->mf_blocknr_min = -1;
   mfp->mf_neg_count = 0;
   mfp->pagesInFile = mfp->mf_blocknr_max;

   //Compute maximum number of pages ('maxmem' is in Kbyte):
   //  'mammem' * 1Kbyte / page-size-in-bytes.
   //Avoid overflow by first reducing page size as much as possible.
   {
   int       shift = 10;
   unsigned    page_size = mfp->pageSize;

   while (shift > 0 && (page_size & 1) == 0) {
       page_size = page_size >> 1;
       --shift;
   }
   mfp->usedCountMax = (p_mm << shift) / page_size;
   if (mfp->usedCountMax < 10)
       mfp->usedCountMax = 10;
   }

   return mfp;
}

// Open a file for an existing memfile. Used when updatecount set from 0 to some value. If the 
// file already exists, this fails. "fname" is the name of file to use (NULL means no file at all)
// Note: "fname" must have been allocated, it is not copied!  If opening the file fails, "fname" 
// is freed.
// return value: FAIL if file could not be opened, OK otherwise
pub int
mf_open_file(MemFile* mfp, CS fname) {
   mf_do_open(mfp, fname, O_RDWR|O_CREAT|O_EXCL); // try to open the file

   if (mfp->fd < 0)
      return FAIL;

   mfp->mf_dirty = MF_DIRTY_YES;
   return OK;
}

// Close a memory file and delete the associated file if 'del_file' is true.
pub void
mf_close(MemFile* mfp, int del_file) {
   if (!mfp)          // safety check
      return;
   if (mfp->fd >= 0 && close(mfp->fd) < 0)
      emsg(_(e_close_error_on_swap_file));
   if (del_file && mfp->fName != NULL)
      mch_remove(mfp->fName); // free entries in used list
      
   BlockHeader* nextp;
   for (BlockHeader* hp = mfp->usedFirst; hp; hp = nextp) {
      total_mem_used -= (Ulong)hp->pageCount * mfp->pageSize;
      nextp = hp->bh_next;
      mf_free_bhdr(hp);
   }
   while (mfp->freeFirst)       // free entries in free list
      eeglFree(mf_rem_free(mfp));
   mf_hash_free(&mfp->mf_hash);
   mf_hash_free_all(&mfp->mf_trans);       // free hashtable and its items
   eeglFree(mfp->fName);
   eeglFree(mfp->fullFName);
   eeglFree(mfp);
}

// Close the swap file for a memfile.  Used when 'swapfile' is reset.
pub void
mf_close_file(Book* book, int getlines) {  // get all lines into memory?
   MemFile* mfp = book->mem.mfile;
   if (!mfp || mfp->fd < 0)      // nothing to close
      return;

   if (getlines) {
      // get all blocks in memory by accessing all lines (clumsy!)
      dontReleaseBlocksS = true;
      for (LineNr lnum = 1; lnum <= book->mem.lineCount; ++lnum)
         (void)memGetLine(book, lnum, false);
      dontReleaseBlocksS = false;
      // TODO: should check if all blocks are really in core
   }

   if (close(mfp->fd) < 0)         // close the file
      emsg(_(e_close_error_on_swap_file));
   mfp->fd = -1;

   if (mfp->fName != NULL) {
      mch_remove(mfp->fName);      // delete the swap file
      EE_CLEAR(mfp->fName);
      EE_CLEAR(mfp->fullFName);
   }
}

// Set new size for a memfile.  Used when block 0 of a swapfile has been read
// and the size it indicates differs from what was guessed.
pub void
mf_new_page_size(MemFile* mfp, unsigned new_size) {
   // Correct the memory used for block 0 to the new size, because it will be
   // freed with that size later on.
   total_mem_used += new_size - mfp->pageSize;
   mfp->pageSize = new_size;
}

// get a new block
//   negative: true if negative block number desired (data block)
pub BlockHeader *
mf_new(MemFile* mfp, int negative, int page_count) {
   CS p;
   
   // new BlockHeader. If we reached the maximum size for the used memory blocks, release one
   // If a BlockHeader is returned, use it and adjust the page_count if necessary.
   BlockHeader* hp = mf_release(mfp, page_count);

   //First block in free list. Decide on the number to use:
   //If there is a free block, use its number.
   //Otherwise use mf_block_min for a negative number, mf_block_max for a positive number.
   BlockHeader* freep = mfp->freeFirst;
   if (!negative && freep != NULL && freep->pageCount >= page_count) {
      // If the block in the free list has more pages, take only the number
      // of pages needed and allocate a new BlockHeader with data
      //
      // If the number of pages matches and mf_release() did not return a
      // BlockHeader, use the BlockHeader from the free list and allocate the data
      //
      // If the number of pages matches and mf_release() returned a BlockHeader,
      // just use the number and free the BlockHeader from the free list
      if (freep->pageCount > page_count) {
         if (!hp && (hp = mf_alloc_bhdr(mfp, page_count)) == NULL)
            return NULL;
         hp->bh_bnum = freep->bh_bnum;
         freep->bh_bnum += page_count;
         freep->pageCount -= page_count;
      } ei (!hp) {      // need to allocate memory for this block
         if ((p = alloc((Unt)mfp->pageSize * page_count)) == NULL)
            return NULL;
         hp = mf_rem_free(mfp);
         hp->bh_data = p;
      } else  {        // use the number, remove entry from free list
         freep = mf_rem_free(mfp);
         hp->bh_bnum = freep->bh_bnum;
         eeglFree(freep);
      }
   } else {  // get a new number
      if (!hp && (hp = mf_alloc_bhdr(mfp, page_count)) == NULL)
         return NULL;
      if (negative) {
         hp->bh_bnum = mfp->mf_blocknr_min--;
         mfp->mf_neg_count++;
      } else {
         hp->bh_bnum = mfp->mf_blocknr_max;
         mfp->mf_blocknr_max += page_count;
      }
   }
   hp->bh_flags = BH_LOCKED | BH_DIRTY;   // new block is always dirty
   mfp->mf_dirty = MF_DIRTY_YES;
   hp->pageCount = page_count;
   mf_ins_used(mfp, hp);
   mf_ins_hash(mfp, hp);

   // Init the data to all zero, to avoid reading uninitialized data.
   // This also avoids that the passwd file ends up in the swap file!
   (void)memset((char *)(hp->bh_data), 0, (Unt)mfp->pageSize * page_count);

   return hp;
}

// Get existing block "nr" with "page_count" pages.
// Note: The caller should first check a negative nr with mf_trans_del()
pub BlockHeader *
mf_get(MemFile* mfp, BlockId nr, int page_count) {
   if (nr >= mfp->mf_blocknr_max || nr <= mfp->mf_blocknr_min)
      return NULL;

   // see if it is in the cache
   BlockHeader* hp = mf_find_hash(mfp, nr);
   if (!hp) {  // not in the hash list
      if (nr < 0 || nr >= mfp->pagesInFile)   // can't be in the file
         return NULL;

      // could check here if the block is in the free list

      //Check if we need to flush an existing block.
      //If so, use that block. If not, allocate a new block.
      hp = mf_release(mfp, page_count);
      if (!hp && page_count > 0)
         hp = mf_alloc_bhdr(mfp, page_count);
      if (!hp)
         return NULL;

      hp->bh_bnum = nr;
      hp->bh_flags = 0;
      hp->pageCount = page_count;
      if (mf_read(mfp, hp) == FAIL) {      // cannot read the block!
         mf_free_bhdr(hp);
         return NULL;
      }
   } else {
      mf_rem_used(mfp, hp);   // remove from list, insert in front below
      mf_rem_hash(mfp, hp);
   }

   hp->bh_flags |= BH_LOCKED;
   mf_ins_used(mfp, hp);   // put in front of used list
   mf_ins_hash(mfp, hp);   // put in front of hash list

   return hp;
}

// release the block *hp
//
//   dirty: Block must be written to file later
//   infile: Block should be in file (needed for recovery)
//
//  no return value, function cannot fail
pub void
mf_put(MemFile* mfp, BlockHeader* hp, int dirty, int infile) {
   Unt flags = hp->bh_flags;

   if ((flags & BH_LOCKED) == 0)
      internalErrMsg(e_block_was_not_locked);
   flags &= ~BH_LOCKED;
   if (dirty) {
      flags |= BH_DIRTY;
      if (mfp->mf_dirty != MF_DIRTY_YES_NOSYNC)
          mfp->mf_dirty = MF_DIRTY_YES;
   }
   hp->bh_flags = flags;
   if (infile)
      mf_trans_add(mfp, hp);       // may translate negative in positive nr
}

// block *hp is no longer in used, may put it in the free list of memfile *mfp
pub void
mf_free(MemFile* mfp, BlockHeader* hp) {
   eeglFree(hp->bh_data);   // free the memory
   mf_rem_hash(mfp, hp);   // get *hp out of the hash list
   mf_rem_used(mfp, hp);   // get *hp out of the used list
   if (hp->bh_bnum < 0) {
      eeglFree(hp);      // don't want negative numbers in free list
      mfp->mf_neg_count--;
   } else
      mf_ins_free(mfp, hp);   // put *hp in the free list
}

//Sync the memory file *mfp to disk. Flags:
// MFS_ALL   If not given, blocks with negative numbers are not synced, even when they are dirty!
// MFS_STOP   Stop syncing when a character becomes available, but sync at least one block.
// MFS_FLUSH  Make sure books are flushed to disk, so they will survive a system crash.
// MFS_ZERO   Only write block 0.
//
//Return FAIL for failure, OK otherwise
pub int
mf_sync(MemFile* mfp, Unt flags) {
   int gotInterruptG_save = gotInterruptG;

   if (mfp->fd < 0) {
      // there is no file, nothing to do
      mfp->mf_dirty = MF_DIRTY_NO;
      return FAIL;
   }

   // Only a CTRL-C while writing will break us here, not one typed previously.
   gotInterruptG = false;

   //sync from last to first (may reduce the probability of an inconsistent
   //file) If a write fails, it is very likely caused by a full filesystem.
   //Then we only try to write blocks within the existing file. If that also fails then we give up.
   int status = OK;
   BlockHeader* hp;
   for (hp = mfp->usedLast; hp != NULL; hp = hp->bh_prev) {
      if (((flags & MFS_ALL) || hp->bh_bnum >= 0)
         && (hp->bh_flags & BH_DIRTY)
         && (status == OK || (hp->bh_bnum >= 0
             && hp->bh_bnum < mfp->pagesInFile))
      ) {
         if ((flags & MFS_ZERO) && hp->bh_bnum != 0)
            continue;
         if (mf_write(mfp, hp) == FAIL) {
            if (status == FAIL)   // double error: quit syncing
               break;
            status = FAIL;
         }
         if (flags & MFS_STOP) {
            // Stop when char available now.
            if (ui_char_avail())
               break;
         } else
            ui_breakcheck();
         if (gotInterruptG)
            break;
      }
   } 

   //If the whole list is flushed, the memfile is not dirty anymore.
   //In case of an error this flag is also set, to avoid trying all the time.
   if (!hp || status == FAIL)
      mfp->mf_dirty = MF_DIRTY_NO;

   if ((flags & MFS_FLUSH) != 0 && p_sws && eeFsync(mfp->fd) != 0)
      status = FAIL;

   gotInterruptG |= gotInterruptG_save;

   return status;
}

// For all blocks in memory file *mfp that have a positive block number set the dirty flag. These 
// are blocks that need to be written to a newly created swapfile.
pub void
mf_set_dirty(MemFile* mfp) {
   for (BlockHeader* hp = mfp->usedLast; hp; hp = hp->bh_prev) {
      if (hp->bh_bnum > 0)
         hp->bh_flags |= BH_DIRTY;
   } 
   mfp->mf_dirty = MF_DIRTY_YES;
}

// insert block *hp in front of hashlist of memfile *mfp
private void
mf_ins_hash(MemFile* mfp, BlockHeader* hp) {
   mf_hash_add_item(&mfp->mf_hash, (MfHashItem *)hp);
}

// remove block *hp from hashlist of memfile list *mfp
private void
mf_rem_hash(MemFile* mfp, BlockHeader* hp) {
   mf_hash_rem_item(&mfp->mf_hash, (MfHashItem *)hp);
}

// look in hash lists of memfile *mfp for block header with number 'nr'
private BlockHeader *
mf_find_hash(MemFile* mfp, BlockId nr) {
   return (BlockHeader *)mf_hash_find(&mfp->mf_hash, nr);
}

// insert block *hp in front of used list of memfile *mfp
private void
mf_ins_used(MemFile* mfp, BlockHeader* hp) {
   hp->bh_next = mfp->usedFirst;
   mfp->usedFirst = hp;
   hp->bh_prev = NULL;
   if (hp->bh_next == NULL)       // list was empty, adjust last pointer
      mfp->usedLast = hp;
   else
      hp->bh_next->bh_prev = hp;
   mfp->mf_used_count += hp->pageCount;
   total_mem_used += (Ulong)hp->pageCount * mfp->pageSize;
}

// remove block *hp from used list of memfile *mfp
private void
mf_rem_used(MemFile* mfp, BlockHeader* hp) {
   if (hp->bh_next == NULL)       // last block in used list
      mfp->usedLast = hp->bh_prev;
   else
      hp->bh_next->bh_prev = hp->bh_prev;
   if (hp->bh_prev == NULL)       // first block in used list
      mfp->usedFirst = hp->bh_next;
   else
      hp->bh_prev->bh_next = hp->bh_next;
   mfp->mf_used_count -= hp->pageCount;
   total_mem_used -= (Ulong)hp->pageCount * mfp->pageSize;
}

// Release the least recently used block from the used list if the number of used memory blocks 
// gets too big.
// Return the block header to the caller, including the memory block, so it can be re-used. Make 
// sure the page_count is right.
// Return NULL if no block is released.
private BlockHeader *
mf_release(MemFile* mfp, int page_count) {

   // don't release while in mf_close_file()
   if (dontReleaseBlocksS)
      return NULL;

   //Need to release a block if the number of blocks for this memfile is
   //higher than the maximum or total memory used is over 'maxmemtot'
   Boole need_release = (mfp->mf_used_count >= mfp->usedCountMax);

   // Try to create a swap file if the amount of memory used is getting too high.
   if (mfp->fd < 0 && need_release && swapEnabledG) {
      // find for which book this memfile is
      Book* book;
      FOR_ALL_BOOKS(book) {
         if (book->mem.mfile == mfp)
            break;
      } 
      if (book && book->maySwap)
         memOpenSwapFile(book);
   }

   //don't release a block if
   //  there is no file for this memfile
   //or
   //  the number of blocks for this memfile is lower than the maximum
   //    and
   //  total memory used is not up to 'maxmemtot'
   if (mfp->fd < 0 || !need_release)
      return NULL;

   BlockHeader* hp;
   for (hp = mfp->usedLast; hp; hp = hp->bh_prev) {
      if (!(hp->bh_flags & BH_LOCKED))
          break;
   } 
   if (!hp)   // not a single one that can be released
      return NULL;

   //If the block is dirty, write it. If the write fails we don't free it.
   if ((hp->bh_flags & BH_DIRTY) && mf_write(mfp, hp) == FAIL)
      return NULL;

   mf_rem_used(mfp, hp);
   mf_rem_hash(mfp, hp);

   // If a BlockHeader is returned, make sure that the page_count of bh_data is right
   if (hp->pageCount != page_count) {
      EE_CLEAR(hp->bh_data);
      if (page_count > 0)
         hp->bh_data = alloc((Unt)mfp->pageSize * page_count);
      if (hp->bh_data == NULL) {
         eeglFree(hp);
         return NULL;
      }
      hp->pageCount = page_count;
   }
   return hp;
}

//release as many blocks as possible
//Used in case of out of memory
//
//return true if any memory was released
pub int
mf_release_all(void){
   Book* book;
   MemFile* mfp;
   BlockHeader* hp;
   int retval = false;

   FOR_ALL_BOOKS(book) {
      mfp = book->mem.mfile;
      if (mfp) {
         // If no swap file yet, may open one
         if (mfp->fd < 0 && book->maySwap)
            memOpenSwapFile(book);

         // only if there is a swapfile
         if (mfp->fd >= 0) {
            for (hp = mfp->usedLast; hp != NULL; ) {
               if (!(hp->bh_flags & BH_LOCKED)
                      && (!(hp->bh_flags & BH_DIRTY) || mf_write(mfp, hp) != FAIL)) {
                  mf_rem_used(mfp, hp);
                  mf_rem_hash(mfp, hp);
                  mf_free_bhdr(hp);
                  hp = mfp->usedLast;   // re-start, list was changed
                  retval = true;
               }
               else
                  hp = hp->bh_prev;
            }
         }
      }
   }
   return retval;
}

// Allocate a block header and a block of memory for it.
private BlockHeader *
mf_alloc_bhdr(MemFile* mfp, int page_count) {
   BlockHeader* hp;

   if ((hp = ALLOC_ONE(BlockHeader)) == NULL)
      return NULL;

   if ((hp->bh_data = alloc((Unt)mfp->pageSize * page_count)) == NULL) {
      eeglFree(hp);       // not enough memory
      return NULL;
   }
   hp->pageCount = page_count;
   return hp;
}

// Free a block header and the block of memory for it.
private void
mf_free_bhdr(BlockHeader* hp) {
   eeglFree(hp->bh_data);
   eeglFree(hp);
}

// Insert entry *hp in the free list.
private void
mf_ins_free(MemFile* mfp, BlockHeader* hp) {
   hp->bh_next = mfp->freeFirst;
   mfp->freeFirst = hp;
}

// remove the first entry from the free list and return a pointer to it
// Note: caller must check that mfp->freeFirst is not NULL!
private BlockHeader *
mf_rem_free(MemFile* mfp) {
   BlockHeader* hp = mfp->freeFirst;
   mfp->freeFirst = hp->bh_next;
   return hp;
}

// Read a block from disk. Return FAIL for failure, OK otherwise
private int
mf_read(MemFile* mfp, BlockHeader* hp) {
   if (mfp->fd < 0)       // there is no file, can't read
      return FAIL;

   Unt page_size = mfp->pageSize;
   FileOffset offset = (FileOffset)page_size * hp->bh_bnum;
   Unt size = page_size * hp->pageCount;
   if (lseek(mfp->fd, offset, SEEK_SET) != offset) {
      PERROR(_(e_seek_error_in_swap_file_read));
      return FAIL;
   }
   if ((unsigned)fiReadEintr(mfp->fd, hp->bh_data, size) != size) {
      PERROR(_(e_read_error_in_swap_file));
      return FAIL;
   }

   return OK;
}

// write a block to disk. Return FAIL for failure, OK otherwise
private int
mf_write(MemFile* mfp, BlockHeader* hp) {
   FileOffset offset;       // offset in the file
   BlockId nr;       // block nr which is being written
   BlockHeader* hp2;
   Unt page_count; // number of pages written
   Unt size;       // number of bytes written

   if (mfp->fd < 0 && !mfp->mf_reopen)
      // there is no file and there was no file, can't write
      return FAIL;

   if (hp->bh_bnum < 0 && mf_trans_add(mfp, hp) == FAIL) // must assign file block number
      return FAIL;

   Unt page_size = mfp->pageSize; //bytes in a page

   // We don't want gaps in the file. Write the blocks in front of *hp to extend the file.
   // If block 'pagesInFile' is not in the hash list, it has been
   // freed. Fill the space in the file with data from the current block.
   for (;;) {
      int attempt;

      nr = hp->bh_bnum;
      if (nr > mfp->pagesInFile) {     // beyond end of file
         nr = mfp->pagesInFile;
         hp2 = mf_find_hash(mfp, nr);   // NULL caught below
      } else
         hp2 = hp;

      offset = (FileOffset)page_size * nr;
      if (hp2 == NULL)       // freed block, fill with dummy data
          page_count = 1;
      else
          page_count = hp2->pageCount;
      size = page_size * page_count;

      for (attempt = 1; attempt <= 2; ++attempt) {
         if (mfp->fd >= 0) {
            if (lseek(mfp->fd, offset, SEEK_SET) != offset) {
               PERROR(_(e_seek_error_in_swap_file_write));
               return FAIL;
            }
            if (mf_write_block(mfp, hp2 == NULL ? hp : hp2, offset, size) == OK)
               break;
         }

         if (attempt == 1) {
            // If the swap file is on a network drive, and the network gets disconnected and then 
            // re-connected, we can maybe fix it by closing and then re-opening the file.
            if (mfp->fd >= 0)
                close(mfp->fd);
            mfp->fd = openRw((char *)mfp->fName, mfp->mf_flags);
            mfp->mf_reopen = (mfp->fd < 0);
         }
         if (attempt == 2 || mfp->fd < 0) {
            //Avoid repeating the error message, this mostly happens when the disk is full. We 
            //give the message again only after a successful write or when hitting a key. We keep 
            //on trying, in case some space becomes available.
            if (!did_swapwrite_msg)
               emsg(_(e_write_error_in_swap_file));
            did_swapwrite_msg = true;
            return FAIL;
         }
      }

      did_swapwrite_msg = false;
      if (hp2 != NULL)          // written a non-dummy block
         hp2->bh_flags &= ~BH_DIRTY;
                      // appended to the file
      if (nr + (BlockId)page_count > mfp->pagesInFile)
         mfp->pagesInFile = nr + page_count;
      if (nr == hp->bh_bnum)          // written the desired block
         break;
    }
    return OK;
}

// Write block "hp" with data size "size" to file "mfp->fd".
// Take care of encryption. Return FAIL or OK.
private int
mf_write_block(MemFile* mfp, BlockHeader* hp, FileOffset, unsigned size) {
   Arr(Byte) data = hp->bh_data;
   int result = OK;

   if ((unsigned)write_eintr(mfp->fd, data, size) != size)
      result = FAIL;

   if (data != hp->bh_data)
      eeglFree(data);

   return result;
}

// Make block number for *hp positive and add it to the translation list
// Return FAIL for failure, OK otherwise
private int
mf_trans_add(MemFile* mfp, BlockHeader* hp) {
   BlockId new_bnum;
   int page_count;

   if (hp->bh_bnum >= 0)          // it's already positive
      return OK;

   NrTranslation* np = ALLOC_ONE(NrTranslation);

   //Get a new number for the block.
   //If the first item in the free list has sufficient pages, use its number
   //Otherwise use mf_blocknr_max.
   BlockHeader* freep = mfp->freeFirst;
   page_count = hp->pageCount;
   if (freep && freep->pageCount >= page_count) {
      new_bnum = freep->bh_bnum;
      // If the page count of the free block was larger, reduce it.
      // If the page count matches, remove the block from the free list
      if (freep->pageCount > page_count) {
          freep->bh_bnum += page_count;
          freep->pageCount -= page_count;
      } else {
          freep = mf_rem_free(mfp);
          eeglFree(freep);
      }
   } else {
      new_bnum = mfp->mf_blocknr_max;
      mfp->mf_blocknr_max += page_count;
   }

   np->nt_old_bnum = hp->bh_bnum;       // adjust number
   np->nt_new_bnum = new_bnum;

   mf_rem_hash(mfp, hp);          // remove from old hash list
   hp->bh_bnum = new_bnum;
   mf_ins_hash(mfp, hp);          // insert in new hash list

   // Insert "np" into "mf_trans" hashtable with key "np->nt_old_bnum"
   mf_hash_add_item(&mfp->mf_trans, (MfHashItem *)np);

   return OK;
}

// Lookup a translation from the trans lists and delete the entry.
// Return the positive new number when found, the old number when not found
pub BlockId
mf_trans_del(MemFile* mfp, BlockId old_nr) {
   NrTranslation* np = (NrTranslation *)mf_hash_find(&mfp->mf_trans, old_nr);

   if (!np)      // not found
      return old_nr;

   mfp->mf_neg_count--;
   BlockId new_bnum = np->nt_new_bnum;

   // remove entry from the trans list
   mf_hash_rem_item(&mfp->mf_trans, (MfHashItem *)np);

   eeglFree(np);

   return new_bnum;
}

// Set mfp->fullFName according to mfp->fName and some other things.
// Only called when creating or renaming the swapfile.   Either way it's a new
// name so we must work out the full path name.
pub void
mf_set_ffname(MemFile* mfp) {
   mfp->fullFName = fiExpandAndCopy(mfp->fName, false);
}

// Make the name of the file used for the memfile a full path. Used before doing a :cd
pub void
mf_fullname(MemFile* mfp) {
   if (!mfp || !mfp->fName || !mfp->fullFName)
      return;

   eeglFree(mfp->fName);
   mfp->fName = mfp->fullFName;
   mfp->fullFName = NULL;
}

// true if there are any translations pending for 'mfp'
pub int
mf_need_trans(MemFile* mfp) {
   return (mfp->fName && mfp->mf_neg_count > 0);
}

// Open a swap file for a memfile.
// The "fname" must be in allocated memory, and is consumed (also when an error occurs).
private void
mf_do_open(MemFile* mfp, CS fname, Unt flags) {      // flags for open()
   mfp->fName = fname;

   //Get the full path name before the open fname cannot be nameBuffG, because it must 
   //have been allocated.
   mf_set_ffname(mfp);

   //Extra security check: When creating a swap file it really shouldn't
   //exist yet. If there is a symbolic link, this is most likely an attack.
   FileStat sb;
   if ((flags & O_CREAT) != 0 && lstat((char *)mfp->fName, &sb) >= 0) {
      mfp->fd = -1;
      emsg(_(e_swap_file_already_exists_symlink_attack));
   } else {
      //try to open the file
      flags |= O_EXTRA | O_NOFOLLOW;
      mfp->mf_flags = flags;
      mfp->fd = openRw((char *)mfp->fName, flags);
   }

   //If the file cannot be opened, use memory only
   if (mfp->fd < 0) {
      EE_CLEAR(mfp->fName);
      EE_CLEAR(mfp->fullFName);
   } else {
      int fdflags = fcntl(mfp->fd, F_GETFD);
      if (fdflags >= 0 && (fdflags & FD_CLOEXEC) == 0)
         (void)fcntl(mfp->fd, F_SETFD, fdflags | FD_CLOEXEC);
   }
}

// Implementation of MfHashTable follows.

//The number of buckets in the hashtable is increased by a factor of
//MHT_GROWTH_FACTOR when the average number of items per bucket
//exceeds 2 ^ MHT_LOG_LOAD_FACTOR.
#define MHT_LOG_LOAD_FACTOR 6
#define MHT_GROWTH_FACTOR   2   // must be a power of two

//Initialize an empty hash table.
private void
mf_hash_init(MfHashTable*  mht) {
   CLEAR_POINTER(mht);
   mht->mht_buckets = mht->mht_small_buckets;
   mht->mask = MHT_INIT_SIZE - 1;
}

//Free the array of a hash table.  Does not free the items it contains!
//The hash table must not be used again without another mf_hash_init() call.
private void
mf_hash_free(MfHashTable *mht) {
   if (mht->mht_buckets != mht->mht_small_buckets)
      eeglFree(mht->mht_buckets);
}

// Free the array of a hash table and all the items it contains.
private void
mf_hash_free_all(MfHashTable* mht) {
   MfHashItem   *mhi;
   MfHashItem   *next;

   for (Ulong idx = 0; idx <= mht->mask; idx++) {
      for (mhi = mht->mht_buckets[idx]; mhi; mhi = next) {
          next = mhi->next;
          eeglFree(mhi);
      }
   }

   mf_hash_free(mht);
}

// Find "key" in hashtable "mht". Return a pointer to a MfHashItem or NULL if the item was not found
private MfHashItem *
mf_hash_find(MfHashTable *mht, BlockId key) {
   MfHashItem* mhi = mht->mht_buckets[key & mht->mask];
   while (mhi && mhi->key != key)
      mhi = mhi->next;

   return mhi;
}

// Add item "mhi" to hashtable "mht". "mhi" must not be NULL.
private void
mf_hash_add_item(MfHashTable* mht, MfHashItem* mhi) {
   Ulong idx = mhi->key & mht->mask;
   mhi->next = mht->mht_buckets[idx];
   mhi->prev = NULL;
   if (mhi->next)
      mhi->next->prev = mhi;
   mht->mht_buckets[idx] = mhi;

   mht->mht_count++;

   // Grow hashtable when we have more thank 2^MHT_LOG_LOAD_FACTOR items per bucket on average
   if (mht->mht_fixed == 0 && (mht->mht_count >> MHT_LOG_LOAD_FACTOR) > mht->mask) {
      if (mf_hash_grow(mht) == FAIL) {
          // stop trying to grow after first failure to allocate memory
          mht->mht_fixed = 1;
      }
   }
}

// Remove item "mhi" from hashtable "mht".
// "mhi" must not be NULL and must have been inserted into "mht".
private void
mf_hash_rem_item(MfHashTable *mht, MfHashItem *mhi) {
   if (mhi->prev == NULL)
      mht->mht_buckets[mhi->key & mht->mask] = mhi->next;
   else
      mhi->prev->next = mhi->next;

   if (mhi->next)
      mhi->next->prev = mhi->prev;

   mht->mht_count--;

   // We could shrink the table here, but it typically takes little memory, so why bother?
}

// Increase number of buckets in the hashtable by MHT_GROWTH_FACTOR and rehash items.
// Returns FAIL when out of memory.
private int
mf_hash_grow(MfHashTable *mht) {
   Ulong       j;
   MfHashItem   *mhi;
   MfHashItem   *tails[MHT_GROWTH_FACTOR];
   MfHashItem   **buckets;

   Unt size = (mht->mask + 1) * MHT_GROWTH_FACTOR * sizeof(void *);
   buckets = lallocZeroed(size, false);
   if (!buckets)
      return FAIL;

   int shift = 0;
   while ((mht->mask >> shift) != 0)
      shift++;

   for (Ulong i = 0; i <= mht->mask; i++) {
      //Traverse the items in the i-th original bucket and move them into
      //MHT_GROWTH_FACTOR new buckets, preserving their relative order
      //within each new bucket.  Preserving the order is important because
      //mf_get() tries to keep most recently used items at the front of each bucket.
      //
      //Here we strongly rely on the fact the hashes are computed modulo a power of two.

      CLEAR_FIELD(tails);

      for (mhi = mht->mht_buckets[i]; mhi != NULL; mhi = mhi->next) {
         j = (mhi->key >> shift) & (MHT_GROWTH_FACTOR - 1);
         if (tails[j] == NULL) {
            buckets[i + (j << shift)] = mhi;
            tails[j] = mhi;
            mhi->prev = NULL;
         } else {
            tails[j]->next = mhi;
            mhi->prev = tails[j];
            tails[j] = mhi;
         }
      }

      for (j = 0; j < MHT_GROWTH_FACTOR; j++) {
         if (tails[j])
            tails[j]->next = NULL;
      }
   }

   if (mht->mht_buckets != mht->mht_small_buckets)
      eeglFree(mht->mht_buckets);

   mht->mht_buckets = buckets;
   mht->mask = (mht->mask + 1) * MHT_GROWTH_FACTOR - 1;

   return OK;
}

//}}}
//{{{builtins. Book related builtin functions

//Mark references in functions of books.
pub Boole
setRefInBooks(int copyID) {
   Boole abort = false;
   Book* bp;
   FOR_ALL_BOOKS(bp) {
      for (Listener* lnr = bp->listener; !abort && lnr; lnr = lnr->next)
         abort = abort || memSetRefInCallback(&lnr->callback, copyID);
      if (abort)
         return true;
         
      abort = abort 
         || memSetRefInCallback(&bp->promptCallback, copyID)
         || memSetRefInCallback(&bp->promptInterrupt, copyID)
         || memSetRefInCallback(bp->o.completeFn, copyID)
         || memSetRefInCallback(bp->o.omniFn, copyID)
         || memSetRefInCallback(bp->o.thesaurusFn, copyID);
      if (!abort && bp->o.completeFn)
         abort = abort || memSetRefInCallback(bp->o.completeFn, copyID);
      if (!abort)
         abort = abort || memSetRefInCallback(bp->o.tagFn, copyID)
                       || memSetRefInCallback(bp->o.findFn, copyID);
      if (abort)
         return true;
   }
   return false;
}

pub Book*
bookFindByName(CS name, Boole curtab_only) {
   return bookFindFileByBookNr(
      booklistFindPattern(name, name + STRLEN(name), true, false, curtab_only)
   );
}

// Find a book by number or exact name.
pub Book*
findBook(Var* avar){
   Book* book = NULL;

   if (avar->tag == VAR_NUMBER)
      book = bookFindFileByBookNr((int)avar->number);
   ei (avar->tag == VAR_STRING && avar->string) {
      book = booklistFindByNameExpandingLinks(avar->string);
      if (!book) {
         //No full path name match, try a match with a URL or a "nofile"
         //book, these don't use the full path.
         FOR_ALL_BOOKS(book) {
            if (book->currFileName
                  && (strStartsWithUrl(book->currFileName) || bt_nofilename(book))
                  && STRCMP(book->currFileName, avar->string) == 0)
               break;
         } 
      }
   }
   return book;
}

// If there is a portal for "curBook", make it the current portal.
private void
findPortalIntoCurBook(void) {
   //The portInfos list should have the portals that recently showed the book, going over this is
   //faster than going over all the portals. Do check the book is still there.
   PortInfo* poInfo;
   FOR_ALL_BOOK_PORTINFOS(curBook, poInfo) {
      if (poInfo->portal && poInfo->portal->book == curBook) {
         curPor = poInfo->portal;
         break;
      }
   }
}

//Used before making a change in "book", which is not the current one: Make
//"book" the current book and find a portal into this book, so that side
//effects are done correctly (e.g., adjusting marks).
//
//Information is saved in "cob" and MUST be restored by calling restoreChangeInOtherBook().
//If this fails then "curBook" will not be equal to "book".
private void
prepareChangeInOtherBook(ChangeOtherBook *cob, Book* book) {
   CLEAR_POINTER(cob);

   // Set "curBook" to the book being changed.  Then make sure there is a
   // portal for it to handle any side effects.
   cob->visualActiveSaved = VIsual_active;
   VIsual_active = false;
   cob->curPorSave = curPor;
   curBook = book;
   findPortalIntoCurBook();  // simplest: find existing portal into "book"

   if (curPor->book != book) {
      //No existing portal into this book. It is dangerous to have
      //curPor->book differ from "curBook", use the autocmd portal.
      curBook = curPor->book;
      auCommPrepareBook(&cob->autocommSave, book);
      if (curBook == book)
         cob->usingAco = true;
   }
}

private void
restoreChangeInOtherBook(ChangeOtherBook* cob) {
   if (cob->usingAco) {
      auCommRestoreBook(&cob->autocommSave);
   } else {
      curPor = cob->curPorSave;
      curBook = curPor->book;
   }
   VIsual_active = cob->visualActiveSaved;
}

//Set line or list of 'em in book "book" to "lines". Any type is allowed and converted to a string
private void
updateLinesFromVars(
   Book* book,
   LineNr lnum_arg,
   Boole append,
   Arr(Var) lines,
   Var* returnVar
) {
   LineNr    lnum = lnum_arg + (append ? 1 : 0);
   CS line = NULL;
   List* l = NULL;
   ListItem* li = NULL;
   long   added = 0;
   LineNr   appendLnum;

   // When using the current book mfile will be set if needed.  Useful when
   // setline() is used on startup.  For other books the book must be loaded.
   Boole isCurBook = book == curBook;
   if (!book || (!isCurBook && book->mem.mfile == NULL) || lnum < 1) {
      returnVar->number = FAIL;
      return;
   }

   // After this don't use "return", goto "cleanup"!
   ChangeOtherBook cob;
   if (!isCurBook)
      // set "curBook" to "book" and find a portal for this book
      prepareChangeInOtherBook(&cob, book);

   if (append)
      // appendbufline() uses the line number below which we insert
      appendLnum = lnum - 1;
   else
      // setbufline() uses the line number above which we insert, we only
      // append if it's below the last line
      appendLnum = curBook->mem.lineCount;

   if (lines->tag == VAR_LIST) {
      l = lines->list;
      if (!l || list_len(l) == 0) {
         // not appending anything always succeeds
         goto done;
      }
      CHECK_LIST_MATERIALIZE(l);
      li = l->first;
   } else
      line = daStringOfVar(lines, false);

   // default result is zero == OK
   for (;;) {
      if (l) {
         // list argument, get next string
         if (!li)
            break;
         eeglFree(line);
         line = daStringOfVar(&li->c, false);
         li = li->next;
      }

      returnVar->number = FAIL;
      if (!line || lnum > curBook->mem.lineCount + 1)
         break;

      // When coming here from Insert mode, sync undo, so that this can be
      // undone separately from what was previously inserted.
      if (u_sync_once == 2) {
          u_sync_once = 1; // notify that u_sync() was called
          u_sync(true);
      }

      if (!append && lnum <= curBook->mem.lineCount) {
         // Existing line, replace it. Removes any existing text properties.
         if (u_savesub(lnum) == OK && ml_replace_len(
               lnum, line, (ColNr)STRLEN(line) + 1, true, true) == OK
         ) {
            changed_bytes(lnum, 0);
            if (isCurBook && lnum == curPor->cursor.lnum)
               check_cursor_col();
            returnVar->number = OK;
         }
      } ei (added > 0 || u_save(lnum - 1, lnum) == OK) {
         // append the line
         ++added;
         if (ml_append(lnum - 1, line, (ColNr)0, false) == OK)
            returnVar->number = OK;
      }

      if (!l)         // only one string argument
         break;
      ++lnum;
   }
   eeglFree(line);

   if (added > 0) {
      Portal       *po;
      Tab   *t;

      appended_lines_mark(appendLnum, added);

      //Only adjust the cursor for books other than the current, unless it is the current portal. 
      //For curBook and other portals it has been done in markAdjust().
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == book
                && (po->book != curBook || po == curPor)
                && po->cursor.lnum > appendLnum)
            po->cursor.lnum += added;
      } 
      check_cursor_col();

      //Only update the portal view if book matches curBook, otherwise
      //the computations will be wrong.
      if (curPor->book == curBook)
         update_topline();
   }

done:
   if (!isCurBook)
      restoreChangeInOtherBook(&cob);
}

// "append(lnum, string/list)" function
pub void
f_append(Var *argvars, OUT Var* returnVar) {
   int      anyEmsgSaved = anyEmsgG;
   LineNr lnum = tv_get_lnum(&argvars[0]);
   if (anyEmsgG == anyEmsgSaved)
      updateLinesFromVars(curBook, lnum, true, argvars + 1, returnVar);
}

// Set or append lines to a book.
private void
setOrAppendLines(Arr(Var) argvars, OUT Var* returnVar, Boole append) {
   int anyEmsgSaved = anyEmsgG;

   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      returnVar->number = FAIL;
   else {
      LineNr lnum = daGetLnumFromBookOrVar(&argvars[1], book);
      if (anyEmsgG == anyEmsgSaved)
         updateLinesFromVars(book, lnum, append, argvars + 2, returnVar);
   }
}

// "appendbufline(book, lnum, string/list)" function
pub void
f_appendbufline(Var *argvars, OUT Var* returnVar) {
   setOrAppendLines(argvars, returnVar, true);
}

// "bufadd(expr)" function
pub void
f_bufadd(Var *argvars, OUT Var* returnVar) {
   CS name = tv_get_string(&argvars[0]);
   returnVar->number = bookOpen(*name == ZERO ? NULL : name, 0);
}

pub void
f_bufexists(Var *argvars, OUT Var* returnVar) {
   returnVar->number = (findBook(&argvars[0]) != NULL);
}

pub void
f_buflisted(Var *argvars, OUT Var* returnVar) {
   Book* book = findBook(&argvars[0]);
   returnVar->number = (book && book->o.bookListed);
}

pub void
f_bufload(Arr(Var) argvars, OUT Var*) {
   Book* book = evGetBookArg(argvars);
   if (book)
      bookEnsureLoaded(book);
}

pub void
f_bufloaded(Var *argvars, OUT Var* returnVar) {
   Book* book = findBook(&argvars[0]);
   returnVar->number = (book && book->mem.mfile);
}

pub void
f_bufname(Var *argvars, OUT Var* returnVar) {
   Var* tv = &argvars[0];
   Book* book = (tv->tag == VAR_UNKNOWN) ? curBook : daGetBookFromArg(tv);
   returnVar->tag = VAR_STRING;
   returnVar->string = (book && book->currFileName) ? copyStr(book->currFileName) : null;
}

pub void
f_bufnr(Var *argvars, OUT Var* returnVar) {
   Boole error = false;
   Book* book = (argvars[0].tag == VAR_UNKNOWN) ? curBook : daGetBookFromArg(&argvars[0]);

   // If the book isn't found and the second argument is not 0, create a new book.
   CS name;
   if (!book
        && argvars[1].tag != VAR_UNKNOWN
        && varGetNumberChk(&argvars[1], OUT &error) != 0
        && !error
        && (name = convertVarToStringSingleUse(&argvars[0])) != NULL
   )
      book = bookNew(name, NULL, (LineNr)1, 0);

   if (book)
      returnVar->number = book->fiNum;
   else
      returnVar->number = -1;
}

private void
bufPortalCommon(Var* argvars, Var* returnVar, int get_nr) {
   Portal   *po;
   int      winnr = 0;
   Book* book = daGetBookFromArg(&argvars[0]);
   FOR_ALL_PORTALS(po) {
      ++winnr;
      if (po->book == book)
         break;
   }
   returnVar->number = (po ? (get_nr ? winnr : po->id) : -1);
}

// "bufwinid(nr)" function
pub void
f_bufwinid(Var *argvars, Var* returnVar) {
   bufPortalCommon(argvars, returnVar, false);
}

// "bufwinnr(nr)" function
pub void
f_bufwinnr(Var *argvars, OUT Var* returnVar){
   bufPortalCommon(argvars, returnVar, true);
}

pub void
f_deletebufline(Var *argvars, OUT Var* returnVar) {
   LineNr   last;
   LineNr   lnum;
   long   count;
   Tab   *tp;
   Portal   *po;
   int      anyEmsgSaved = anyEmsgG;

   returnVar->number = 1;   // FAIL by default

   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      return;

   LineNr first = daGetLnumFromBookOrVar(&argvars[1], book);
   if (anyEmsgG > anyEmsgSaved)
      return;
   if (argvars[2].tag != VAR_UNKNOWN)
      last = daGetLnumFromBookOrVar(&argvars[2], book);
   else
      last = first;

   if (book->mem.mfile == NULL || first < 1 || first > book->mem.lineCount || last < first)
      return;

   // After this don't use "return", goto "cleanup"!
   Boole isCurBook = book == curBook;
   ChangeOtherBook cob;
   if (!isCurBook)
      // set "curBook" to "book" and find a portal into this book
      prepareChangeInOtherBook(&cob, book);

   if (last > curBook->mem.lineCount)
     last = curBook->mem.lineCount;
   count = last - first + 1;

   // When coming here from Insert mode, sync undo, so that this can be
   // undone separately from what was previously inserted.
   if (u_sync_once == 2) {
      u_sync_once = 1; // notify that u_sync() was called
      u_sync(true);
   }

   if (u_save(first - 1, last + 1) == FAIL)
      goto cleanup;

   for (lnum = first; lnum <= last; ++lnum)
      ml_delete_flags(first, ML_DEL_MESSAGE);

   FOR_ALL_TAB_PORTALS(tp, po) {
      if (po->book == book) {
         if (po->cursor.lnum > last)
            po->cursor.lnum -= count;
         ei (po->cursor.lnum > first)
            po->cursor.lnum = first;
         if (po->cursor.lnum > po->book->mem.lineCount)
            po->cursor.lnum = po->book->mem.lineCount;
         po->cacheState = 0;
         if (po->cursor.lnum <= po->topLine)
            po->topLine = 1;
      }
   } 
   check_cursor_col();
   deleted_lines_mark(first, count);
   returnVar->number = 0; // OK

cleanup:
  if (!isCurBook)
     restoreChangeInOtherBook(&cob);
}

// Find the lnum for the book 'book' for the current portal.
private LineNr
findLnum(Book* book) {
   return bookFindFpos(book)->lnum;
}

// Return book options, variables and other attributes in a dictionary.
private Bag *
getBookInfo(Book* book) {
   Tab   *tp;
   Portal   *po;

   Bag* bag = allocBag();
   bagAddNumber(bag, S"bufnr", book->fiNum);
   bagAddString(bag, S"name", book->fullFileName);
   bagAddNumber(bag, S"lnum", book == curBook ? curPor->cursor.lnum : findLnum(book));
   bagAddNumber(bag, S"linecount", book->mem.lineCount);
   bagAddNumber(bag, S"loaded", book->mem.mfile != null);
   bagAddNumber(bag, S"listed", book->o.bookListed);
   bagAddNumber(bag, S"changed", bookWasChanged(book));
   bagAddNumber(bag, S"changedtick", CHANGEDTICK(book));
   bagAddNumber(bag, S"hidden", book->mem.mfile && book->countPortals == 0);
   bagAddNumber(bag, S"command", book == commPortBookG);

   // Get a reference to book variables
   bagAddBag(bag, S"variables", book->bVars);

   // List of portals displaying this book
   List* portals = list_alloc();
   FOR_ALL_TAB_PORTALS(tp, po) {
      if (po->book == book)
         list_append_number(portals, (Long)po->id);
   } 
   bagAddList(bag, S"portals", portals);

   // List of popup portals displaying this book
   portals = list_alloc();
   FOR_ALL_POPUPPORTS(po) {
      if (po->book == book)
         list_append_number(portals, (Long)po->id);
   } 
   FOR_ALL_TABS(tp) {
      FOR_ALL_POPUPPORTS_IN_TAB(tp, po) {
         if (po->book == book)
            list_append_number(portals, (Long)po->id);
      } 
   } 

   bagAddList(bag, S"popups", portals);

   if (book->signList) {
      // List of signs placed in this book
      List* signs = list_alloc();
      llGetBookSigns(book, signs);
      bagAddList(bag, S"signs", signs);
   }

   bagAddNumber(bag, S"lastused", book->lastUsed);

   return bag;
}

pub void
f_getbufinfo(Var *argvars, OUT Var* returnVar) {
   Book   *book = NULL;
   Book   *argbuf = NULL;
   int      filtered = false;
   int      sel_buflisted = false;
   int      sel_bufloaded = false;
   int      sel_bufmodified = false;

   allocReturnList(returnVar);

   // List of all the books or selected books
   if (argvars[0].tag == VAR_BAG) {
      Bag* selB = argvars[0].bag;

      if (selB) {
         filtered = true;
         sel_buflisted = bagGetBool(selB, tConst("buflisted"), false);
         sel_bufloaded = bagGetBool(selB, tConst("bufloaded"), false);
         sel_bufmodified = bagGetBool(selB, tConst("bufmodified"), false);
      }
   } ei (argvars[0].tag != VAR_UNKNOWN) {
      //Information about one book. Argument specifies the book
      argbuf = daGetBookFromArg(&argvars[0]);
      if (!argbuf)
         return;
   }

   //Return information about all the books or a specified book
   FOR_ALL_BOOKS(book) {
      if (argbuf && argbuf != book)
         continue;
      if (filtered && ((sel_bufloaded && book->mem.mfile == NULL)
            || (sel_buflisted && !book->o.bookListed)
            || (sel_bufmodified && !book->wasModified)))
         continue;

      Bag* b = getBookInfo(book);
      if (b)
         listAppendBag(returnVar->list, b);
      if (argbuf)
         return;
   }
}

//Get line or list of lines from book "book" into "returnVar".
//Return a range (from start to end) of lines in returnVar from the specified book.
//If 'retlist' is true, then the lines are returned as an Eegl List.
private void
getLinesIntoVar(
   Book   *book,
   LineNr   start,
   LineNr   end,
   int      retlist,
   OUT Var   *returnVar
) {
   CS p;

   if (retlist) {
      allocReturnList(returnVar);
   } else {
      returnVar->tag = VAR_STRING;
      returnVar->string = NULL;
   }

   if (!book || book->mem.mfile == NULL || start < 0)
      return;
      
   if (!retlist) {
      p = (start >= 1 && start <= book->mem.lineCount) ? memGetLine(book, start, false) : S"";
      returnVar->string = copyStr(p);
   } else {
      if (end < start)
         return;

      if (start < 1)
         start = 1;
      if (end > book->mem.lineCount)
         end = book->mem.lineCount;
      while (start <= end)
         if (list_append_string(returnVar->list, memGetLine(book, start++, false), -1) == FAIL)
            break;
   }
}

//"retlist" true: "getBookLineIntoVar()" function
//"retlist" false: "getbufoneline()" function
private void
getBookLineIntoVar(Var* argvars, Var* returnVar, int retlist) {
   LineNr   lnum = 1;
   LineNr   end = 1;
   int      anyEmsgSaved = anyEmsgG;

   Book* book = daGetBookFromArg(&argvars[0]);
   if (book) {
      lnum = daGetLnumFromBookOrVar(&argvars[1], book);
      if (anyEmsgG > anyEmsgSaved)
         return;
      if (argvars[2].tag == VAR_UNKNOWN)
         end = lnum;
      else
         end = daGetLnumFromBookOrVar(&argvars[2], book);
    }

    getLinesIntoVar(book, lnum, end, retlist, returnVar);
}

pub void
f_getbufline(Var *argvars, OUT Var* returnVar) {
   getBookLineIntoVar(argvars, returnVar, true);
}

pub void
f_getbufoneline(Var *argvars, OUT Var* returnVar) {
   getBookLineIntoVar(argvars, returnVar, false);
}

//"getline(lnum, [end])" function
pub void
f_getline(Var *argvars, OUT Var* returnVar) {
   LineNr   end;
   int      retlist;
   LineNr lnum = tv_get_lnum(argvars);
   if (argvars[1].tag == VAR_UNKNOWN) {
      end = 0;
      retlist = false;
   } else {
      end = tv_get_lnum(&argvars[1]);
      retlist = true;
   }

   getLinesIntoVar(curBook, lnum, end, retlist, returnVar);
}

pub void
f_setbufline(Var *argvars, OUT Var* returnVar) {
   setOrAppendLines(argvars, returnVar, false);
}

pub void
f_setline(Var *argvars, OUT Var* returnVar) {
   int      anyEmsgSaved = anyEmsgG;

   LineNr lnum = tv_get_lnum(&argvars[0]);
   if (anyEmsgG == anyEmsgSaved)
      updateLinesFromVars(curBook, lnum, false, argvars + 1, returnVar);
}

//}}}
//{{{charset

private int parseAnIsOption(CS var, Book* book, Boole only_check);
private int win_nolbr_chartabsize(CharTableSize *cts, int *headp);

private Boole chartab_initialized = false;

// charsForKeywords[] is an array of 32 bytes, each bit representing one of the
// characters 0-255.
#define SET_CHARTAB(book, c) (book)->charsForKeywords[(unsigned)(c) >> 3] |= (1 << ((c) & 0x7))
#define RESET_CHARTAB(book, c) (book)->charsForKeywords[(unsigned)(c) >> 3] &= ~(1 << ((c) & 0x7))
#define GET_CHARTAB(book, c) ((book)->charsForKeywords[(unsigned)(c) >> 3] & (1 << ((c) & 0x7)))

// table used below, see initCharTable() for an explanation
private Byte charTableP[256];

// Flags for charTableP[].
#define CT_CELL_MASK   0x07   // mask: nr of display cells (1, 2 or 4)
#define CT_PRINT_CHAR  0x10   // flag: set for printable chars
#define CT_ID_CHAR     0x20   // flag: set for ID chars
#define CT_FNAME_CHAR  0x40   // flag: set for file name chars

private int inPortalBorder(Portal *po, ColNr vcol);

//Fill charTableP[]. Also fill curBook->charsForKeywords[] with flags for keyword
//characters for current book.
//
//Depend on the settings @iskeyword, @isident, @isfname
//
//The contents of charTableP[]:
//- The lower two bits of every byte, masked by CT_CELL_MASK, give the number of display
//  cells the character occupies (1 or 2). Not valid for UTF-8 above 0x80.
//- CT_PRINT_CHAR bit is set when the character is printable (no need to translate the character 
//before displaying it).  Note that no characters can have 2 display cells and still be printable.
//- CT_FNAME_CHAR bit is set when the character can be in a file name.
//- CT_ID_CHAR bit is set when the character can be in an identifier.
//
//Return FAIL if @iskeyword, @isident or @isfname option has an error, OK otherwise.
private int
initCharTable(Book* book) {
   //Init word char flags all to false
   CLEAR_FIELD(book->charsForKeywords);

   // Walk through the 'isident', 'iskeyword', 'isfname' and 'isprint' options.
   for (Unt i = 0; i < 3; ++i) {
      CS p;
      if (i == 0)
         p = p_isi;      // first round: 'isident'
      ei (i == 1)
         p = p_isf;      // third round: 'isfname'
      else   // i == 2
         p = book->o.isKeyword;   // fourth round: 'iskeyword'
         
      if (p && parseAnIsOption(p, book, false) == FAIL)
         return FAIL;
   }

   chartab_initialized = true;
   return OK;
}

pub int
bookInitCharsForKeywordsForCurbook(void) {
   return curBook ? initCharTable(curBook) : OK;
}

pub void
bookInitGlobalCharTable() {
   //Set the default size for printable characters:
   //From <Space> to '~' is 1 (printable), others are 2 (not printable).
   //This also inits all 'isident' and 'isfname' flags to false.
   Unt c = 0;
   for (; c < ' '; c++)
      charTableP[c] = 2;
   for (; c <= '~'; c++)
      charTableP[c] = 1 + CT_PRINT_CHAR;
   for (; c < 256; c++) {
      // UTF-8: bytes 0xa0 - 0xff are printable (latin1)
      if (c >= 0xa0)
         charTableP[c] = CT_PRINT_CHAR + 1;
       else // the rest is unprintable by default
         charTableP[c] = 2;
   }

   // Assume that every multi-byte char is a filename character.
   for (c = 0xa0; c < 256; c++) {
      charTableP[c] |= CT_FNAME_CHAR;
   } 
}

//Strict version of bookIsCharPrintable(c), don't return true if "c" is the head byte of a double-byte 
//character
pub int
bookIsCharPrintable_strict(int c) {
   if (c >= 0x100)
      return utf_printable(c);
   return (c >= 0x100 || (c > 0 && (charTableP[c] & CT_PRINT_CHAR) != 0));
}

//Parse an "is" option: @iskeyword, @isident, @isfname, @isprint. Return OK/FAIL.
private int
parseAnIsOption(CS var, Book* book, Boole only_check) {  // false: refill charTableP[]
   CS p = var;
   long c;
   int tilde;

   //Parse the 'isident', 'iskeyword', 'isfname' and 'isprint' options. Each option is a list of 
   //characters, character numbers or ranges, separated by commas, e.g.: "200-210,x,#-178,-"
   while (*p != ZERO) {
      tilde = false;
      Boole do_isalpha = false;
      if (*p == '^' && p[1] != ZERO) {
         tilde = true;
         ++p;
      }
      if (EE_ISDIGIT(*p))
         c = parseLong(&p);
      else
         c = strAdvanceMultibyte(&p);
         
      Unt c2 = UNT;
      if (*p == '-' && p[1] != ZERO) {
         ++p;
         if (EE_ISDIGIT(*p))
            c2 = parseLong(&p);
         else
            c2 = strAdvanceMultibyte(&p);
         c2 = *p++;
      }
      if (c <= 0 || c >= 256 || (c2 < c && c2 != UNT) || c2 >= 256 || !(*p == ZERO || *p == ','))
         return FAIL;

      Boole trail_comma = *p == ',';
      p = skip_to_option_part(p);
      if (trail_comma && *p == ZERO)
         // Trailing comma is not allowed.
         return FAIL;

      if (only_check)
         continue;

      if (c2 == UNT) {   // not a range
         //A single '@' (not "@-@"):
         //Decide on letters being ID/printable/keyword chars with
         //standard function isalpha(). This takes care of locale for single-byte characters).
         if (c == '@') {
            do_isalpha = true;
            c = 1;
            c2 = 255;
         } else
            c2 = c;
      }

      for (; c <= c2; c++) {
         if (!do_isalpha || MB_ISLOWER(c) || MB_ISUPPER(c)) {
            if (var == p_isi) {         // (re)set ID flag
               if (tilde)
                  charTableP[c] &= ~CT_ID_CHAR;
               else
                  charTableP[c] |= CT_ID_CHAR;
            } ei (var == p_isf) {        // (re)set fname flag
               if (tilde)
                  charTableP[c] &= ~CT_FNAME_CHAR;
               else
                  charTableP[c] |= CT_FNAME_CHAR;
            } else {// var == book->o.isKeyword (re)set keyword flag
               if (tilde)
                  RESET_CHARTAB(book, c);
               else
                  SET_CHARTAB(book, c);
            }
         }
      }
   }

   return OK;
}

//Catch 22: charTableP[] can't be initialized before the options are initialized, and initializing 
//options may cause transchar() to be called!
//When chartab_initialized == false don't use charTableP[].
//Does NOT work for multi-byte characters, c must be <= 255.
//Also doesn't work for the first byte of a multi-byte, "c" must be a character!
private Byte translateScratch[7];

pub CS
transchar(Unt c) {
   return transchar_buf(c);
}

pub CS
transchar_buf(Unt c) {
   int i = 0;
   if (IS_SPECIAL(c)) {      // special key code, display as ~@ char
      translateScratch[0] = '~';
      translateScratch[1] = '@';
      i = 2;
      c = K_SECOND(c);
   }

   if ((!chartab_initialized && ((c >= ' ' && c <= '~'))) || (c < 256 && bookIsCharPrintable_strict(c))) {
      // printable character
      translateScratch[i] = c;
      translateScratch[i + 1] = ZERO;
   } else
      transchar_nonprint(translateScratch + i, c);
   return translateScratch;
}

//Return number of display cells occupied by byte "b". Caller must make sure 0 <= b <= 255.
//For multi-byte mode "b" must be the first byte of a character.
//A TAB is counted as two cells: "^I".
//Will return 0 for bytes >= 0x80, because the number of cells depends on further bytes.
pub int
byte2cells(Unt b) {
   if (b >= 0x80)
      return 0;
   return (charTableP[b] & CT_CELL_MASK);
}

//Return number of display cells occupied by character "c".
//"c" can be a special key (negative number) in which case 3 or 4 is returned.
//A TAB is counted as two cells: "^I" or four: "<09>".
pub int
bookChar2Cells(Unt c) {
   if (IS_SPECIAL(c))
      return bookChar2Cells(K_SECOND(c)) + 2;
   if (c >= 0x80) {
      // UTF-8: above 0x80 need to check the value
      return mb_char2cells(c);
   } else {
      return (charTableP[c & 0xff] & CT_CELL_MASK);
   }
}

//Return number of display cells occupied by character at "*p".
//A TAB is counted as two cells: "^I" or four: "<09>".
pub int
bookPtr2Cells(CS p) {
   //For UTF-8 we need to look at more bytes if the first byte is >= 0x80.
   if (*p >= 0x80)
      return mb_ptr2cells(p);
   return (charTableP[*p] & CT_CELL_MASK);
}

//Like transchar_buf(), but called with a byte instead of a character. Check
//for an illegal UTF-8 byte.
pub CS
bookTranscharByte(Unt c) {
   if (c >= 0x80) {
      transchar_nonprint(translateScratch, c);
      return translateScratch;
   }
   return transchar_buf(c);
}

//Return the number of characters 'c' will take on the screen, taking into account the size of a tab
//Use a define to make it fast, this is used very often!!! Also see getvcol() below.
# define RET_PORT_BOOK_CHARSIZE(po, book, p, col) \
   if (*(p) == TAB && (!(po)->o.list || listCharsG.tab1)) { \
      int ts; \
      ts = (book)->o.shiftWidth; \
      return (int)(ts - (col % ts)); \
   } else \
      return bookPtr2Cells(p);

pub int
chartabsize(CS p, ColNr col) {
   RET_PORT_BOOK_CHARSIZE(curPor, curBook, p, col)
}

pub int
win_chartabsize(Portal *po, CS p, ColNr col) {
   RET_PORT_BOOK_CHARSIZE(po, po->book, p, col)
}

// Return the number of characters the string "s" will take on the screen, taking into account the 
// size of a tab. Does not handle text properties, since "s" is not a book line.
pub Unt
linetabsize_str(CS s) {
   return linetabsize_col(0, s);
}

// Like linetabsize_str(), but "s" starts at column "startcol".
pub int
linetabsize_col(int startcol, CS s) {
   CharTableSize cts;

   bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, startcol, s, s);
   Long vcol = cts.cts_vcol;

   while (*cts.cts_ptr != ZERO) {
      vcol += lbr_chartabsize_adv(&cts);
      if (vcol > MAXCOL) {
         cts.cts_vcol = MAXCOL;
         break;
      } else
         cts.cts_vcol = (int)vcol;
   }
   clear_chartabsize_arg(&cts);
   return (int)cts.cts_vcol;
}

//Like linetabsize_str(), but for a given portal instead of the current one.
//Doesn't count the size of @listchars "eol".
pub Unt
drawLineOnScreentabsize(Portal* po, LineNr lnum, CS line, ColNr len) {
   CharTableSize cts;
   bookInitCharsForKeywordsSizeArg(&cts, po, lnum, 0, line, line);
   drawLineOnScreentabsize_cts(&cts, len);
   clear_chartabsize_arg(&cts);
   return (Unt)cts.cts_vcol;
}

//Return the number of cells line "lnum" of portal "po" will take on the screen, taking into 
//account the size of a tab and text properties. Doesn't count the size of @listchars "eol".
pub int
linetabsize(Portal *po, LineNr lnum) {
   return drawLineOnScreentabsize(po, lnum, memGetLine(po->book, lnum, false), (ColNr)MAXCOL);
}


// Like linetabsize(), but counts the size of 'listchars' "eol".
pub int
linetabsize_eol(Portal *po, LineNr lnum) {
   return linetabsize(po, lnum) + ((po->o.list && listCharsG.eol != ZERO) ? 1 : 0);
}

//Like linetabsize(), but excludes 'above'/'after'/'right'/'below' aligned
//virtual text, while keeping inline virtual text.
pub int
linetabsize_no_outer(Portal *po, LineNr lnum) {
   CharTableSize cts;
   CS line = memGetLine(po->book, lnum, false);

   bookInitCharsForKeywordsSizeArg(&cts, po, lnum, 0, line, line);

   if (cts.cts_text_prop_count) {
      int write_idx = 0;
      for (int read_idx = 0; read_idx < cts.cts_text_prop_count; read_idx++) {
         TextProp *tp = &cts.cts_text_props[read_idx];
         if (tp->col != MAXCOL) {
            if (read_idx != write_idx)
               cts.cts_text_props[write_idx] = *tp;
            write_idx++;
         }
      }
      cts.cts_text_prop_count = write_idx;
      if (cts.cts_text_prop_count == 0)
         EE_CLEAR(cts.cts_text_props);
    }

    drawLineOnScreentabsize_cts(&cts, (ColNr)MAXCOL);
    clear_chartabsize_arg(&cts);
    return (int)cts.cts_vcol;
}

pub void
drawLineOnScreentabsize_cts(CharTableSize *cts, ColNr len) {
   Long vcol = cts->cts_vcol;
   cts->cts_with_trailing = len == MAXCOL;
   for ( ; *cts->cts_ptr != ZERO && (len == MAXCOL || cts->cts_ptr < cts->cts_line + len);
         MB_PTR_ADV(cts->cts_ptr)
   ){
      vcol += win_lbr_chartabsize(cts, NULL);
      if (vcol > MAXCOL) {
         cts->cts_vcol = MAXCOL;
         break;
      } else
         cts->cts_vcol = (int)vcol;
   }
   // check for a virtual text at the end of a line or on an empty line
   if (len == MAXCOL && cts->cts_has_prop_with_text && *cts->cts_ptr == ZERO) {
      (void)win_lbr_chartabsize(cts, NULL);
      vcol += cts->cts_cur_text_width;
      // when properties are above or below the empty line must also be counted
      if (cts->cts_ptr == cts->cts_line && cts->cts_prop_lines > 0)
          ++vcol;
      cts->cts_vcol = vcol > MAXCOL ? MAXCOL : (int)vcol;
   }
}

//Return true if 'c' is a normal identifier character: Letters and chars from the 'isident' option
pub int
eeIsIdentifierChar(int c) {
   return (c > 0 && c < 0x100 && (charTableP[c] & CT_ID_CHAR));
}

// Like eeIsIdentifierChar() but not using the 'isident' option: letters, numbers and underscore
pub int
eeIsNormalIdentifierChar(int c) {
   return ASCII_ISALNUM(c) || c == '_';
}

pub int
eeIsWordc_buf(Unt c, Book* book) {
   if (c >= 0x100) {
      return utf_class_buf(c, book) >= 2;
   }
   return (c < UNT_NEG && GET_CHARTAB(book, c) != 0);
}

//return true if 'c' is a keyword character: Letters and characters from @iskeyword option for the 
//current book. For multi-byte characters mb_get_class() is used (builtin rules).
pub int
eeIsWordc(Unt c) {
   return eeIsWordc_buf(c, curBook);
}

pub int
eeIsWordPtr_buf(CS p, Book* book) {
   Unt c = *p;
   if (utf8CharLens[c] > 1)
      c = mb_ptr2char(p);
   return eeIsWordc_buf(c, book);
}

// Just like eeIsWordc() but uses a pointer to the (multi-byte) character.
pub int
eeIsWordPtr(CS p) {
   return eeIsWordPtr_buf(p, curBook);
}

// Return true if 'c' is a valid file-name character as specified with the 'isfname' option.
// Assume characters above 0x100 are valid (multi-byte). To be used for commands like "gf".
pub int
eeIsFnameChar(Unt c) {
   return (c >= 0x100 || (c < UNT_NEG && (charTableP[c] & CT_FNAME_CHAR)));
}

//Return true if 'c' is a valid file-name character, including characters left
//out of 'isfname' to make "gf" work, such as comma, space, '@', etc.
pub Boole
eeIsFnameCharForGf(Unt c) {
   return eeIsFnameChar(c) || c == ',' || c == ' ' || c == '@';
}

//Return true if 'c' is a printable character.
//Assume characters above 0x100 are printable (multi-byte), except for Unicode.
pub Boole
bookIsCharPrintable(Unt c) {
   if (c > 0xFF)
      return utf_printable(c);
   return (c > 0 && (charTableP[c] & CT_PRINT_CHAR) != 0);
}

//Prepare the structure passed to chartabsize functions.
//"line" is the start of the line, "ptr" is the first relevant character.
//When "lnum" is zero do not use text properties that insert text.
pub void
bookInitCharsForKeywordsSizeArg(
   OUT CharTableSize* cts,
   Portal* po,
   LineNr lnum,
   ColNr col,
   CS line,
   CS ptr
) {
   CLEAR_POINTER(cts);
   cts->cts_win = po;
   cts->cts_vcol = col;
   cts->cts_line = line;
   cts->cts_ptr = ptr;
   cts->cts_bri_size = -1;
   if (lnum > 0 && !ignore_text_props) {
      CS propStart;
      int count = get_text_props(OUT &propStart, po->book, lnum, false);
      cts->cts_text_prop_count = count;
      if (count > 0) {
         // Make a copy of the properties, so that they are properly
         // aligned.  Make it twice as long for the sorting below.
         cts->cts_text_props = ALLOC_MULT(TextProp, count * 2);
         MEMMOVE(cts->cts_text_props + count, propStart, count * sizeof(TextProp));
         for (int i = 0; i < count; ++i) {
            TextProp *tp = cts->cts_text_props + i + count;
            if (tp->id < 0 && text_prop_type_valid(po->book, tp)) {
               cts->cts_has_prop_with_text = true;
               break;
            }
         }
         if (!cts->cts_has_prop_with_text) {
             // won't use the text properties, free them
             EE_CLEAR(cts->cts_text_props);
             cts->cts_text_prop_count = 0;
         } else {
            // Need to sort the array to get any truncation right. Do the sorting in the second
            // part of the array, then move the sorted props to the first part of the array.
            Arr(int) text_prop_idxs = ALLOC_MULT(int, count);
            for (int i = 0; i < count; ++i)
               text_prop_idxs[i] = i + count;
            sort_text_props(curBook, cts->cts_text_props, text_prop_idxs, count);
            // Here we want the reverse order.
            for (int i = 0; i < count; ++i)
                cts->cts_text_props[count - i - 1] = cts->cts_text_props[text_prop_idxs[i]];
            eeglFree(text_prop_idxs);
         }
      }
   }
}

// Free any allocated item in "cts".
pub void
clear_chartabsize_arg(OUT CharTableSize* cts) {
   if (cts->cts_text_prop_count > 0) {
      EE_CLEAR(cts->cts_text_props);
      cts->cts_text_prop_count = 0;
   }
}

// Like chartabsize(), but also check for line breaks on the screen and text properties that insert
pub int
lbr_chartabsize(CharTableSize* cts) {
   if (!p_sbr && !curPor->o.breakIndent && !cts->cts_has_prop_with_text) {
      if (curPor->o.wrap)
         return win_nolbr_chartabsize(cts, NULL);
      RET_PORT_BOOK_CHARSIZE(curPor, curBook, cts->cts_ptr, cts->cts_vcol)
   }
   return win_lbr_chartabsize(cts, NULL);
}

// Call lbr_chartabsize() and advance the pointer.
pub int
lbr_chartabsize_adv(CharTableSize *cts) {
   int retval = lbr_chartabsize(cts);
   MB_PTR_ADV(cts->cts_ptr);
   return retval;
}


//Return the screen size of the character indicated by "cts".
//"cts->cts_cur_text_width" is set to the extra size for a text property that inserts text.
//This function is used very often, keep it fast!!!!
//
//If "headp" not NULL, set "*headp" to the size of 'showbreak'/'breakindent'
//included in the return value.
//When "cts->cts_max_head_vcol" is positive, only count in "*headp" the size
//of 'showbreak'/'breakindent' before "cts->cts_max_head_vcol".
//When "cts->cts_max_head_vcol" is negative, only count in "*headp" the size
//of 'showbreak'/'breakindent' before where cursor should be placed.
//
//Warning: "*headp" may not be set if it's 0, init to 0 before calling.
pub int
win_lbr_chartabsize(CharTableSize* cts, int* headp){
   Portal* po = cts->cts_win;
   CS line = cts->cts_line; // start of the line
   CS s = cts->cts_ptr;
   ColNr vcol = cts->cts_vcol;
   int mb_added = 0;
   int n;
   Boole no_sbr = false;

   cts->cts_cur_text_width = 0;
   cts->cts_first_char = 0;

   // No @showbreak, @breakindent and text properties that insert text: finish quickly
   if (!po->o.breakIndent && !p_sbr && !cts->cts_has_prop_with_text) {
      if (po->o.wrap)
         return win_nolbr_chartabsize(cts, headp);
      RET_PORT_BOOK_CHARSIZE(po, po->book, s, vcol)
   }

   int has_lcs_eol = po->o.list && listCharsG.eol != ZERO;

   //First get the normal size, without text properties
   int size = win_chartabsize(po, s, vcol);
   if (*s == ZERO) {
      // 1 cell for EOL list char (if present), as opposed to the two cell ^@
      // for a ZERO character in the text.
      size = has_lcs_eol ? 1 : 0;
   }
   int is_doublewidth = size == 2 && utf8CharLens[*s] > 1;

   if (cts->cts_has_prop_with_text) {
      int tab_size = size;
      int charlen = *s == ZERO ? 1 : utfCharLen(s);
      int i;
      int col = (int)(s - line);
      ArrayList    *gap = &po->book->textPropText;

      // The "$" for 'list' mode will go between the EOL and the text prop, account for that.
      if (has_lcs_eol) {
         ++vcol;
         --size;
      }

      for (i = 0; i < cts->cts_text_prop_count; ++i) {
         TextProp* tp = cts->cts_text_props + i;
         int col_off = normalPortalColumnOffset(po);

         //Watch out for the text being deleted.  "cts_text_props" is a
         //copy, the text prop may actually have been removed from the line.
         if (tp->id < 0
               && ((tp->col - 1 >= col && tp->col - 1 < col + charlen)
                     || (tp->col == MAXCOL 
                           && ((tp->flags & TEXT_PROP_ALIGN_ABOVE) 
                              ? col == 0 
                              : (s[0] == ZERO && cts->cts_with_trailing))
                        )
                  )
               && -tp->id - 1 < gap->len
         ) {
            CS p = ((Byte **)gap->c)[-tp->id - 1];

            if (p) {
               int cells;

               if (tp->col == MAXCOL) {
                  int n_extra = (int)STRLEN(p);

                  cells = text_prop_position(
                     po, tp, vcol, (vcol + size) % (po->width - col_off) + col_off, &n_extra, &p, 
                     NULL, NULL, false
                  );
                  no_sbr = true;  // don't use @showbreak now
               } else
                  cells = eeglStrSize(p);
               cts->cts_cur_text_width += cells;
               if (tp->flags & TEXT_PROP_ALIGN_ABOVE)
                  cts->cts_first_char += cells;
               else
                  size += cells;
               cts->cts_start_incl = tp->flags & TEXT_PROP_START_INCL;
               if (*s == TAB) {
                  // tab size changes because of the inserted text
                  size -= tab_size;
                  tab_size = win_chartabsize(po, s, vcol + size);
                  size += tab_size;
               }
               if (tp->col == MAXCOL 
                     && (tp->flags & (TEXT_PROP_ALIGN_ABOVE | TEXT_PROP_ALIGN_BELOW))
               )  // count extra line for property above/below
                  ++cts->cts_prop_lines;
            }
         }
         if (tp->col != MAXCOL && tp->col - 1 > col)
            break;
      }
      if (has_lcs_eol) {
         --vcol;
         ++size;
      }
   }

   if (is_doublewidth && po->o.wrap && inPortalBorder(po, vcol + size - 2)) {
      ++size;      // Count the ">" in the last column.
      mb_added = 1;
   }

   // May have to add something for 'breakindent' and/or 'showbreak'
   // string at the start of a screen line.
   int head = mb_added;
   CS sbr = no_sbr || !p_sbr ? S"" : p_sbr;
   // When "size" is 0, no new screen line is started.
   if (size > 0 && po->o.wrap && (*sbr != ZERO || po->o.breakIndent)) {
      int col_off_prev = normalPortalColumnOffset(po);
      int width2 = po->width - col_off_prev;
      ColNr wcol = vcol + col_off_prev;
      wcol -= po->virtColFirstChar;
      ColNr max_head_vcol = cts->cts_max_head_vcol;
      int added = 0;

      // cells taken by 'showbreak'/'breakindent' before current char
      int   head_prev = 0;
      if (wcol >= (int)po->width) {
         wcol -= po->width;
         col_off_prev = po->width - width2;
         if (wcol >= width2 && width2 > 0)
            wcol %= width2;
         if (*sbr != ZERO)
            head_prev += eeglStrSize(sbr);
         if (po->o.breakIndent) {
            if (cts->cts_bri_size < 0)
               cts->cts_bri_size = getBreakindentForPort(po, line);
            head_prev += cts->cts_bri_size;
         }
         if (wcol < head_prev) {
            head_prev -= wcol;
            wcol += head_prev;
            added += head_prev;
            if (max_head_vcol <= 0 || vcol < max_head_vcol)
               head += head_prev;
         } else
            head_prev = 0;
         wcol += col_off_prev;
      }

      if (wcol + size > (int)po->width) {
         // cells taken by 'showbreak'/'breakindent' halfway current char
         int   head_mid = 0;
         if (*sbr != ZERO)
            head_mid += eeglStrSize(sbr);
         if (po->o.breakIndent) {
            if (cts->cts_bri_size < 0)
               cts->cts_bri_size = getBreakindentForPort(po, line);
            head_mid += cts->cts_bri_size;
         }
         if (head_mid > 0) {
            // Calculate effective portal width.
            int prev_rem = po->width - wcol;
            int width = width2 - head_mid;

            if (width <= 0)
               width = 1;
            // Divide "size - prev_rem" by "width", rounding up.
            int cnt = (size - prev_rem + width - 1) / width;
            added += cnt * head_mid;

            if (max_head_vcol == 0 || vcol + size + added < max_head_vcol)
                head += cnt * head_mid;
            ei (max_head_vcol > vcol + head_prev + prev_rem)
                head += (max_head_vcol - (vcol + head_prev + prev_rem)
                          + width2 - 1) / width2 * head_mid;
            ei (max_head_vcol < 0) {
               int off = mb_added;
               if (*s != ZERO && ((stateG & MODE_NORMAL) || cts->cts_start_incl))
                  off += cts->cts_cur_text_width;
               if (off >= prev_rem)
                  head += (1 + (off - prev_rem) / width) * head_mid;
            }
         }
      }

      size += added;
   }

   if (headp)
      *headp = head;

   Boole need_lbr = false;
   if (need_lbr) {
      // Count all characters from first non-blank after a blank up to next non-blank after a blank.
      int numberextra = normalPortalColumnOffset(po);
      ColNr col_adj = size - 1;
      ColNr colmax = (ColNr)(po->width - numberextra - col_adj);
      if (vcol >= colmax) {
         colmax += col_adj;
         n = colmax;
         if (n > 0)
            colmax += (((vcol - colmax) / n) + 1) * n - col_adj;
      }

      ColNr vcol2 = vcol;
      for (;;) {
         CS ps = s;
         MB_PTR_ADV(s);
         int c = *s;
         if (!(c != ZERO
                && (EE_ISBREAK(c)
                  || (!EE_ISBREAK(c) && (vcol2 == vcol || !EE_ISBREAK((int)*ps))))))
            break;

         vcol2 += win_chartabsize(po, s, vcol2);
         if (vcol2 >= colmax) {     // doesn't fit
            size = colmax - vcol + col_adj;
            break;
         }
      }
   }

   size += cts->cts_first_char;
   return size;
}

//Like win_lbr_chartabsize(), except that we know 'linebreak' is off, 'wrap'
//is on and there are no properties that insert text.  This means we need to
//check for a double-byte character that doesn't fit at the end of the screen line.
//Only uses "cts_win", "cts_ptr" and "cts_vcol" from "cts".
private int
win_nolbr_chartabsize(CharTableSize* cts, int* headp){
   Portal* po = cts->cts_win;
   CS s = cts->cts_ptr;
   ColNr col = cts->cts_vcol;
   int n;

   if (*s == TAB && (!po->o.list || listCharsG.tab1)) {
      n = po->book->o.shiftWidth;
      return (int)(n - (col % n));
   }
   n = bookPtr2Cells(s);
   // Add one cell for a double-width character in the last column of the
   // portal, displayed with a ">".
   if (n == 2 && utf8CharLens[*s] > 1 && inPortalBorder(po, col)) {
      if (headp)
         *headp = 1;
      return 3;
   }
   return n;
}

// Return true if virtual column "vcol" is in the rightmost column of portal "po".
private int                                                                    
inPortalBorder(Portal *po, ColNr vcol) {
   if (po->width == 0)   // there is no border
      return false;
   int width1 = po->width - normalPortalColumnOffset(po); //width of first line (after line number)
   if ((int)vcol < width1 - 1)
      return false;
   if ((int)vcol == width1 - 1)
      return true;
   if (width1 <= 0)
      return false;
   return ((vcol - width1) % width1 == width1 - 1);
}


//Get virtual column number of pos.
// start: on the first position of this character (TAB, ctrl)
//cursor: where the cursor is on this character (first char, except for TAB)
//   end: on the last position of this character (TAB, ctrl)
//
//This is used very often, keep it fast!
pub void
getvcol(
   Portal* po,
   Pos* pos,
   ColNr* start,
   ColNr* cursor,
   ColNr* end
) {
   int incr;
   int head;
   int ts = po->book->o.shiftWidth;
   CharTableSize cts;
   int      on_ZERO = false;

   ColNr vcol = 0;
   CS ptr = memGetLine(po->book, pos->lnum, false);  // points to current char
   CS line = ptr;  // start of the line

   bookInitCharsForKeywordsSizeArg(&cts, po, pos->lnum, 0, line, line);
   cts.cts_max_head_vcol = -1;

   //This function is used very often, do some speed optimizations.
   //When 'list', 'linebreak', 'showbreak' and 'breakindent' are not set
   //and there are no text properties with "text" use a simple loop.
   //Also use this when 'list' is set but tabs take their normal size.
   
   Unt c;
   if ((!po->o.list || listCharsG.tab1 != ZERO)
       && !p_sbr && !po->o.breakIndent
       && !cts.cts_has_prop_with_text
   ) {
      for (;;) {
         head = 0;
         c = *ptr;
         // make sure we don't go past the end of the line
         if (c == ZERO) {
            incr = 1;   // ZERO at end of line only takes one column
            break;
         }
         // A tab gets expanded, depending on the current column
         if (c == TAB)
            incr = ts - (vcol % ts);
         else {
            //If the byte is >= 0x80, need to look at further bytes to find the cell width.
            if (c >= 0x80)
               incr = mb_ptr2cells(ptr);
            else
               incr = charTableP[c] & CT_CELL_MASK;

            //If a double-cell char doesn't fit at the end of a line
            //it wraps to the next line, it's like this char is three cells wide.
            if (incr == 2 && po->o.wrap && utf8CharLens[*ptr] > 1 && inPortalBorder(po, vcol)
            ) {
               ++incr;
               head = 1;
            }
         }

         CS next_ptr = ptr + utfCharLen(ptr);
         if (next_ptr - line > pos->col) // character at pos->col
            break;

          vcol += incr;
          ptr = next_ptr;
      }
   } else {
      for (;;) {
         //A tab gets expanded, depending on the current column. Other things also take up space.
         head = 0;
         incr = win_lbr_chartabsize(&cts, &head);
         // make sure we don't go past the end of the line
         if (*cts.cts_ptr == ZERO) {
            incr = 1;   // ZERO at end of line only takes one column
            if (cts.cts_cur_text_width > 0)
                incr = cts.cts_cur_text_width;
            on_ZERO = true;
            break;
         }
         if (cursor == &po->virtCol && cts.cts_ptr == cts.cts_line)
            // do not count the virtual text above for cursWant
            po->virtColFirstChar = cts.cts_first_char;

         CS next_ptr = cts.cts_ptr + utfCharLen(cts.cts_ptr);
         if (next_ptr - line > pos->col) // character at pos->col
            break;

         cts.cts_vcol += incr;
         cts.cts_ptr = next_ptr;
      }
      vcol = cts.cts_vcol;
      ptr = cts.cts_ptr;
   }
   clear_chartabsize_arg(&cts);

   if (*ptr == ZERO && pos->col < MAXCOL && pos->col > ptr - line)
      pos->col = ptr - line;

   if (start)
      *start = vcol + head;
   if (end)
      *end = vcol + incr - 1;
   if (cursor) {
      if (*ptr == TAB
         && (stateG & MODE_NORMAL)
         && !po->o.list
         && !virtual_active()
         && !(VIsual_active
               && LTOREQ_POS(*pos, VIsual))
         )
          *cursor = vcol + incr - 1;       // cursor at end
      else {
         // in Insert mode, if "start_incl" is true the text gets inserted
         // after the virtual text, thus add its width
         if (((stateG & MODE_INSERT) == 0 || cts.cts_start_incl) && !on_ZERO)
            // cursor is after inserted text, unless on the ZERO
            vcol += cts.cts_cur_text_width;
         else
            // insertion also happens after the "above" virtual text
            vcol += cts.cts_first_char;
         *cursor = vcol + head;       // cursor at start
      }
   }
}

// Get virtual cursor column in the current portal, pretending 'list' is off.
pub ColNr
getvcol_nolist(Pos* posp) {
   int   list_save = curPor->o.list;
   ColNr vcol;

   curPor->o.list = false;
   if (posp->coladd)
      bookGetVirtualColInVirtualMode(curPor, posp, NULL, &vcol, NULL);
   else
      getvcol(curPor, posp, NULL, &vcol, NULL);
   curPor->o.list = list_save;
   return vcol;
}

// Get virtual column in virtual mode.
pub void
bookGetVirtualColInVirtualMode(
   Portal* po,
   Pos* pos,
   OUT ColNr* start,
   OUT ColNr* cursor,
   OUT ColNr* end
) {
   ColNr   col;
   ColNr   coladd;
   ColNr   endadd;

   if (virtual_active()) {
      // For virtual mode, only want one value
      getvcol(po, pos, &col, NULL, NULL);

      coladd = pos->coladd;
      endadd = 0;
      // Cannot put the cursor on part of a wide character.
      CS ptr = memGetLine(po->book, pos->lnum, false);
      if (pos->col < memGetBookLen(po->book, pos->lnum)) {
         Unt c = mb_ptr2char(ptr + pos->col);

         if (c != TAB && bookIsCharPrintable(c)) {
            endadd = (ColNr)(bookChar2Cells(c) - 1);
            if (coladd > endadd)   // past end of line
               endadd = 0;
            else
               coladd = 0;
         }
      }
      col += coladd;
      if (start)
         *start = col;
      if (cursor)
         *cursor = col;
      if (end)
          *end = col + endadd;
  } else
     getvcol(po, pos, start, cursor, end);
}

//Get the leftmost and rightmost virtual column of pos1 and pos2. Used for Visual block mode.
pub void
getvcols(
   Portal* po,
   Pos* pos1,
   Pos* pos2,
   ColNr* left,
   ColNr* right
) {
   ColNr   from1, from2, to1, to2;

   if (LT_POSP(pos1, pos2)) {
      bookGetVirtualColInVirtualMode(po, pos1, &from1, NULL, &to1);
      bookGetVirtualColInVirtualMode(po, pos2, &from2, NULL, &to2);
   } else {
      bookGetVirtualColInVirtualMode(po, pos2, &from1, NULL, &to1);
      bookGetVirtualColInVirtualMode(po, pos1, &from2, NULL, &to2);
   }
   if (from2 < from1)
      *left = from2;
   else
      *left = from1;
   if (to2 > to1) {
      *right = to2;
   } else
      *right = to1;
}


//}}}

// Determines how deeply nested %{} blocks will be evaluated in statusline.
# define MAX_STL_EVAL_DEPTH 100

//{{{book

typedef dev_t Device;

#define FOR_ALL_BOOKS_FROM_LAST(book) \
    for ((book) = lastBook; (book); (book) = (book)->prev)

private CS msg_loclist = S"[Location List]";
private CS msg_qflist = S"[Quickfix List]";

// Number of times freeBook() was called.
private int freeCallCountS = 0;

private int   top_file_num = 1;   // highest file number
private ArrayList recycledFileNumberS = GA_EMPTY;   // file numbers to recycle

// Calculate the percentage that `part` is of the `whole`.
private int
calc_percentage(long part, long whole) {
   // With 32 bit longs and more than 21,474,836 lines multiplying by 100
   // causes an overflow, thus for large numbers divide instead.
   return (part > 1000000L) ? (int)(part / (whole / 100L)) : (int)((part * 100L) / whole);
}

// The highest possible book number.
pub int get_highest_fnum(void) {
   return top_file_num - 1;
}

//Convert the specified character index of line 'lnum' in book to a byte index. Works only 
//for loaded books. Return -1 on failure. The index of the first byte and the first character is 0
pub int
bookCharidxToByteidx(Book* book, int lnum, int charidx) {
   if (!book || book->mem.mfile == NULL)
      return -1;

   if (lnum > book->mem.lineCount)
      lnum = book->mem.lineCount;

   CS str = memGetLine(book, lnum, false);
   if (str == NULL)
      return -1;

   // Convert the character offset to a byte offset
   CS t = str;
   while (*t != ZERO && --charidx > 0)
      t += utfCharLen(t);

    return t - str;
}

// Read data from book for retrying.
private int
readBook(
   int read_stdin,       // read file from stdin, otherwise fifo
   Invocation* invo,          // for forced 'ff' or NULL
   Unt      flags          // extra flags for readfile()
){
   int retval = OK;
   LineNr   line_count;

   // Read from the book which the text is already filled in and append at the end. 
   line_count = curBook->mem.lineCount;
   retval = readfile(
       read_stdin ? NULL : curBook->fullFileName,
       read_stdin ? NULL : curBook->currFileName,
       line_count, (LineNr)0, (LineNr)MAXLNUM, invo,
       flags | READ_BOOK);
   if (retval == OK) {
      // Delete the binary lines.
      while (--line_count >= 0)
         ml_delete((LineNr)1);
   } else {
      // Delete the converted lines.
      while (curBook->mem.lineCount > line_count)
         ml_delete(line_count);
   }
   // Put the cursor on the first line.
   curPor->cursor.lnum = 1;
   curPor->cursor.col = 0;

   if (read_stdin) {
      //Set or reset 'modified' before executing autocommands, so that it can be changed there.
      if (!CURBOOK_EMPTY())
         changed();
      ei (retval == OK)
         unchanged(curBook, true);
   }
   return retval;
}

// Ensure book is loaded. Do not trigger the swap-exists action.
pub void
bookEnsureLoaded(Book* book) {
   if (book->mem.mfile)
      return;

   // Make sure there is a portal into the book. Otherwise skip it.
   AutocommSave   aco;
   auCommPrepareBook(&aco, book);
   if (curBook == book) {
      if (swap_exists_action != SEA_READONLY)
         swap_exists_action = SEA_NONE; 
      bookOpenFromInvo(false, NULL, 0);
      auCommRestoreBook(&aco);
   }
}

//Open current book, that is: open the memfile and read the file into memory. Return OK/FAIL
pub Unt
bookOpenFromInvo(
   Boole read_stdin,     //read file from stdin
   Invocation* invo, //for forced 'ff' or NULL
   Unt flags        //extra flags for readfile()
){
   int      retval = OK;
   BookRef   oldCurBook;
   int      read_fifo = false;

   if (optImmutableMode() && curBook->fullFileName)
      curBook->o.modifiable = false;

   if (ml_open(curBook) == FAIL) {
      // There MUST be a memfile, otherwise we can't do anything
      // If we can't create one for the current book, take another book
      bookClose(NULL, curBook, 0, false, false);
      FOR_ALL_BOOKS(curBook) {
         if (curBook->mem.mfile)
            break;
      } 
      // If there is no memfile at all, exit. This is OK, since there are no changes to lose.
      if (!curBook) {
         emsg(_(e_cannot_allocate_any_buffer_exiting));
         // Don't try to do any saving, with "curBook" NULL almost nothing will work.
         v_dying = 2;
         exitEegl(2);
      }

      emsg(_(e_cannot_allocate_book_using_other_one));
      enterBook(curBook);
      return FAIL;
   }

   // Do not sync this book yet, may first want to read the file.
   if (curBook->mem.mfile)
      curBook->mem.mfile->mf_dirty = MF_DIRTY_YES_NOSYNC;

   //The autocommands in readfile() may change the book, but only AFTER reading the file.
   bookStoreInRef(OUT &oldCurBook, curBook);
   curBook->modifiedWasSet = false;

   // mark cursor position as being invalid
   curPor->cacheState = 0;

   // A book without an actual file should not use the book name to read a file.
   if (bt_nofileread(curBook))
      flags |= READ_NOFILE;

   // Read the file if there is one.
   if (curBook->fullFileName) {
      int old_msg_silent = msg_silent;
      Boole save_bin = curBook->o.binary;
      int perm;
      perm = mch_getperm(curBook->fullFileName);
      if (perm >= 0 && (S_ISFIFO(perm) || S_ISSOCK(perm)))
         read_fifo = true;
      if (read_fifo)
         curBook->o.binary = true;
      retval = readfile(
         curBook->fullFileName, curBook->currFileName,
         (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, invo,
         flags | READ_NEW | (read_fifo ? READ_FIFO : 0)
      );
      if (read_fifo) {
         curBook->o.binary = save_bin;
         if (retval == OK)
            retval = readBook(false, invo, flags);
      }
      msg_silent = old_msg_silent;
      // Help book is filtered.
      if (bookIsHelp(curBook))
         searchFixHelpBook();
   } ei (read_stdin) {
      Boole save_bin = curBook->o.binary;

      //First read the text in binary mode into the book.
      //Then read from that same book and append at the end.
      curBook->o.binary = true;
      retval = readfile(
         NULL, NULL, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, NULL, flags | (READ_NEW + READ_STDIN)
      );
      curBook->o.binary = save_bin;
      if (retval == OK)
         retval = readBook(true, invo, flags);
   }

   // Can now sync this book in ml_sync_all().
   if (curBook->mem.mfile && curBook->mem.mfile->mf_dirty == MF_DIRTY_YES_NOSYNC) {
      curBook->mem.mfile->mf_dirty = MF_DIRTY_YES;
   }

   // Set/reset the Changed flag first, autocmds may change the book.
   // Apply the automatic commands.
   //
   // When reading stdin, the book contents always needs writing, so set
   // the changed flag.  Unless in readonly mode: "ls | gview -".
   if (curBook->modifiedWasSet) {   // autocmd did ":set modified"
      changed();
   } ei (retval == OK && !read_stdin && !read_fifo) {
      unchanged(curBook, true);
   }

   // Set last_changedtick to avoid triggering a TextChanged autocommand right
   // after it was added.
   curBook->lastChangeTick = CHANGEDTICK(curBook);
   curBook->lastChangeTickInsert = CHANGEDTICK(curBook);
   curBook->lastChangeTickPum = CHANGEDTICK(curBook);

   // require "!" to overwrite the file, because it wasn't read completely
   if (aborting())
      curBook->flags |= BF_READERR;

   // Need to update automatic folding.  Do this before the autocommands,
   // they may use the fold info.
   foldUpdateAll(curPor);

   // need to set topLine, unless some autocommand already did that.
   if (!(curPor->cacheState & VALID_TOPLINE)) {
      curPor->topLine = 1;
      curPor->topFill = 0;
   }
   applyAutocommsRetval(EVENT_BUFENTER, NULL, NULL, false, curBook, &retval);

   if (retval != OK)
      return retval;

   // The autocommands may have changed the current book.
   if (bookRefValid(&oldCurBook) && oldCurBook.c->mem.mfile) {

      //Go to the book that was opened, make sure there is a portal into it.
      //If not then skip it.
      AutocommSave   aco;
      auCommPrepareBook(OUT &aco, oldCurBook.c);
      if (curBook == oldCurBook.c) {
         curBook->flags &= ~(BF_CHECK_RO | BF_NEVERLOADED);

         if ((flags & READ_NOWINENTER) == 0)
            applyAutocommsRetval(EVENT_BUFWINENTER, NULL, NULL, false, curBook, &retval);

         // restore curPor/curBook and a few other things
         auCommRestoreBook(&aco);
      }
   }

   return retval;
}

// Store "book" in "bookRef" and set the free count.
pub void
bookStoreInRef(OUT BookRef *bookRef, Book* book){
   bookRef->c = book;
   bookRef->fnum = book == NULL ? 0 : book->fiNum;
   bookRef->freeCount = freeCallCountS;
}

//Return true if "bookRef->c" points to the same book as when bookStoreInRef() was called and it 
//is a valid book. Only goes through the book list if freeCallCountS changed.
//Also checks if fiNum is still the same, a :bwipe followed by :new might get
//the same allocated memory, but it's a different book.
pub Boole
bookRefValid(BookRef* bookRef){
   return bookRef->freeCount == freeCallCountS
      ? true : (bookIsValid(bookRef->c) && bookRef->fnum == bookRef->c->fiNum);
}

//Return true if "book" points to a valid book (in the book list).
//This can be slow if there are many books, prefer using bookRefValid().
pub Boole
bookIsValid(Book* book){
   // Assume that we more often have a recent book, start with the last one.
   Book* bp;
   FOR_ALL_BOOKS_FROM_LAST(bp) {
      if (bp == book)
         return true;
   } 
   return false;
}

// A hash table used to quickly lookup a book by its number.
private EeSet buf_hashtab;

private void
addBookToHashtable(Book* book){
   eeSnprintf(book->keyContainer, sizeof(book->keyContainer - 1), "%x", book->fiNum);
   if (book->key.len == 0) {
      book->key = text(book->keyContainer);
   }
   if (hash_add(&buf_hashtab, book->key, S"create book") == FAIL)
      emsg(_(e_book_cannot_be_registered));
}

private void
removeBookFromHashtable(Book* book) {
   EeSetItem* hi = hash_find(&buf_hashtab, book->key);

   if (!HASHITEM_EMPTY(hi))
      hash_remove(&buf_hashtab, hi, S"close book");
}

//Return true when book "book" can be unloaded.
//Give an error message and return false when the book is locked or the
//screen is being redrawn and the book is in a portal.
private Boole
canUnloadBook(Book* book) {
   Boole canUnload = !book->locked;

   if (canUnload && updating_screen) {
      Portal* po;
      FOR_ALL_PORTALS(po)
         if (po->book == book) {
            canUnload = false;
            break;
         }
   }
   if (!canUnload) {
      CS fname = book->currFileName ? book->currFileName : book->fullFileName;

      showErrFmtMsg(_(e_attempt_to_delete_buffer_that_is_in_use_str), fname ? fname : S"[No Name]");
   }
   return canUnload;
}

//Close the link to a book.
//"action" is used when there is no longer a portal into the book. It can be:
//0                   book becomes hidden
//DOBOOK_UNLOAD       book is unloaded
//DOBOOK_DEL          book is unloaded and removed from book list
//DOBOOK_WIPE         book is unloaded and really deleted
//DOBOOK_WIPE_REUSE   idem, and add to recycledFileNumberS list
//When doing all but the first one on the current book, the caller should
//get a new book very soon!
//
//When "abort_if_last" is true then do not close the book if autocommands
//cause there to be only one portal into this book.  e.g. when ":quit" is
//supposed to close the portal but autocommands close all other portals.
//
//When "ignore_abort" is true don't abort even when aborting() returns true.
//
//Return true when we got to the end and countPortals was decremented.
pub int
bookClose(
   Portal* port,      // if not NULL, set lastCursor
   Book* book,
   Unt action,
   int abort_if_last,
   int ignore_abort
){
   int nPortals;
   BookRef bookRef;
   Boole isCurPor = (curPor && curPor->book == book);
   Portal* theCurPor = curPor;
   Tab* theCurtab = curtab;
   int unload_buf = (action != 0);
   int wipe_buf = (action == DOBOOK_WIPE || action == DOBOOK_WIPE_REUSE);
   int del_buf = (action == DOBOOK_DEL || wipe_buf);

   CHECK_CURBOOK;

   //The caller must take care of NOT deleting/freeing (otherwise we could never free or delete 
   //a book).
   //depending on how we get here countPortals may already be zero
   if (bt_terminal(book) && (book->countPortals <= 1 || del_buf)) {
      CHECK_CURBOOK;
      if (term_job_running(book->term)) {
         if (wipe_buf || unload_buf) {
            if (!canUnloadBook(book))
               return false;

            // Wiping out or unloading a terminal book kills the job.
            free_terminal(book);

            // A terminal book is wiped out when job has finished.
            del_buf = true;
            unload_buf = true;
            wipe_buf = true;
         } else {
            // The job keeps running, hide the book.
            del_buf = false;
            unload_buf = false;
         }
      } ei (!del_buf) {
          // Hide a terminal book.
          unload_buf = false;
      } else {
         if (del_buf || unload_buf) {
            // A terminal book is wiped out if the job has finished.
            // We only do this when there's an intention to unload the
            // book. This way, :hide and other similar commands won't
            // wipe the book.
            del_buf = true;
            unload_buf = true;
            wipe_buf = true;
         }
      }
      CHECK_CURBOOK;
    }

   //Disallow deleting the book when it is locked (already being closed or
   //halfway a command that relies on it). Unloading is allowed.
   if ((del_buf || wipe_buf) && !canUnloadBook(book))
      return false;

   //check no autocommands closed the portal
   if (port && doesPortalExistInAnyTab(port)) {
      //Set lastCursor when closing the last portal into the book.
      //Remember the last cursor position and portal options of the book.
      //This used to be only for the current portal, but then options like
      //@foldmethod may be lost with a ":only" command.
      if (book->countPortals == 1)
         set_last_cursor(port);
      bookSetPosInPort(book, port,
             port->cursor.lnum == 1 ? 0 : port->cursor.lnum,
             port->cursor.col, true
      );
   }

   bookStoreInRef(OUT &bookRef, book);

   // When the book is no longer in a portal, trigger BufWinLeave
   if (book->countPortals == 1) {
      ++book->locked;
      ++book->lockedSplit;
      if (applyAutocomms(EVENT_BUFWINLEAVE, book->currFileName, book->currFileName, false, book)
         && !bookRefValid(&bookRef)
      ) {
         // Autocommands deleted the book.
   aucmd_abort:
         emsg(_(e_autocommands_caused_command_to_abort));
         return false;
      }
      --book->locked;
      --book->lockedSplit;
      if (abort_if_last && onePortal())
          // Autocommands made this the only portal.
          goto aucmd_abort;

      // When the book becomes hidden, but is not unloaded, trigger BufHidden
      if (!unload_buf) {
         ++book->locked;
         ++book->lockedSplit;
         if (applyAutocomms(EVENT_BUFHIDDEN, book->currFileName, book->currFileName, false, book)
                && !bookRefValid(&bookRef))
            // Autocommands deleted the book.
            goto aucmd_abort;
         --book->locked;
         --book->lockedSplit;
         if (abort_if_last && onePortal())
            // Autocommands made this the only portal.
            goto aucmd_abort;
      }
      // autocmds may abort script processing
      if (!ignore_abort && aborting())
         return false;
   }

   // If the book was in curPor and the portal has changed, go back to that
   // portal, if it still exists.  This avoids that ":edit x" triggering a
   // "tabnext" BufUnload autocmd leaves a portal behind without a book.
   if (isCurPor && curPor != theCurPor &&  doesPortalExistInAnyTab(theCurPor)) {
      block_autocmds();
      goto_tab_port(theCurtab, theCurPor);
      unblock_autocmds();
   }

   nPortals = book->countPortals;

   // decrease the link count from portals (unless not in any portal)
   if (book->countPortals > 0)
      --book->countPortals;

   if (diffopt_hiddenoff() && !unload_buf && book->countPortals == 0)
      diffDeleteBook(book);   // Clear 'diff' for hidden book.

   // Return when a portal is displaying the book or when it's not unloaded.
   if (book->countPortals > 0 || !unload_buf)
      return false;

   // Always remove the book when there is no file name.
   if (!book->fullFileName)
      del_buf = true;

   //When closing the current book stop Visual mode before freeing anything.
   if (book == curBook && VIsual_active
#if defined(EXITFREE)
       && !entered_free_all_mem
#endif
   )
      end_visual_mode();

   //Free all things allocated for this book.
   // Also calls the "BufDelete" autocommands when del_buf is true.
   //
   // Remember if we are closing the current book.  Restore the number of
   // portals, so that autocommands in bookFreeAll() don't get confused.
   Boole isCurBook = (book == curBook);
   book->countPortals = nPortals;

   bookFreeAll(book, (del_buf ? BFA_DEL : 0)
         + (wipe_buf ? BFA_WIPE : 0)
         + (ignore_abort ? BFA_IGNORE_ABORT : 0));

   // Autocommands may have deleted the book.
   if (!bookRefValid(&bookRef))
      return false;
   // autocmds may abort script processing
   if (!ignore_abort && aborting())
      return false;

   //It's possible that autocommands change curBook to the one being deleted. This might cause the 
   //previous curBook to be deleted unexpectedly. But in some cases it's OK to delete the curBook, 
   //because a new one is obtained anyway. Therefore only return if curBook changed to the deleted 
   //book.
   if (book == curBook && !isCurBook)
      return false;

   if (doesPortalExistInAnyTab(port) && port->book == book)
      port->book = NULL;  // make sure we don't use the book now

   //Autocommands may have opened or closed portals into this book.
   //Decrement the count for the close we do here.
   if (book->countPortals > 0)
      --book->countPortals;

   //Remove the book from the list.
   if (wipe_buf) {
      Tab   *tp;
      Portal      *po;

      // Do not wipe out the book if it is open in a portal.
      if (book->countPortals > 0)
         return false;

      FOR_ALL_TAB_PORTALS(tp, po)
         mark_forget_file(po, book->fiNum);

      if (action == DOBOOK_WIPE_REUSE) {
         // we can re-use this book number, store it
         if (recycledFileNumberS.ga_itemsize == 0)
            ga_init2(&recycledFileNumberS, sizeof(int), 50);
         if (ga_grow(&recycledFileNumberS, 1) == OK)
            ((int *)recycledFileNumberS.c)[recycledFileNumberS.len++] = book->fiNum;
      }
      if (book->shortFileName != book->fullFileName)
          EE_CLEAR(book->shortFileName);
      else
          book->shortFileName = NULL;
          
      EE_CLEAR(book->fullFileName);
      if (book->prev == NULL)
         firstBook = book->next;
      else
         book->prev->next = book->next;
      if (!book->next)
         lastBook = book->prev;
      else
         book->next->prev = book->prev;
      freeBook(book);
   } else {
      if (del_buf) {
         // Make it look like a new book.
         book->flags = BF_CHECK_RO | BF_NEVERLOADED;

         // Init the options when loaded again.
         book->o.initialized = false;
      }
      buf_clear_file(book);
      if (del_buf)
          book->o.bookListed = false;
   }
   //NOTE: at this point "curBook" may be invalid!
   return true;
}

// Make book not contain a file.
pub void
buf_clear_file(Book* book){
   book->mem.lineCount = 1;
   unchanged(book, true);
   book->startEof = false;
   book->startEol = true;
   book->mem.mfile = NULL;
   book->mem.flags = ML_EMPTY;      // empty book
}

//bookFreeAll() - free all things allocated for a book that are related to
//the file.  Careful: get here with "curPor" NULL when exiting.
//flags:
//BFA_DEL        book is going to be deleted
//BFA_WIPE        book is going to be wiped out
//BFA_KEEP_UNDO     do not free undo information
//BFA_IGNORE_ABORT  don't abort even when aborting() returns true
pub void
bookFreeAll(Book* book, Unt flags){
   Boole isCurBook = (book == curBook);
   Boole isCurPor = (curPor && curPor->book == book);
   Portal* theCurPor = curPor;
   Tab* theCurtab = curtab;

   // Make sure the book isn't closed by autocommands.
   ++book->locked;
   ++book->lockedSplit;
   BookRef   bookRef;
   bookStoreInRef(OUT &bookRef, book);
   if (book->mem.mfile) {
      if (applyAutocomms(EVENT_BUFUNLOAD, book->currFileName, book->currFileName,
                             false, book) && !bookRefValid(&bookRef))
         // autocommands deleted the book
         return;
   }
   if ((flags & BFA_DEL) && book->o.bookListed) {
      if (applyAutocomms(EVENT_BUFDELETE, book->currFileName, book->currFileName, false, book)
            && !bookRefValid(&bookRef))
        // autocommands deleted the book
        return;
    }
   if (flags & BFA_WIPE) {
      if (applyAutocomms(EVENT_BUFWIPEOUT, book->currFileName, book->currFileName, false, book)
            && !bookRefValid(&bookRef))
          // autocommands deleted the book
          return;
   }
   --book->locked;
   --book->lockedSplit;

   // If the book was in curPor and the portal has changed, go back to that
   // portal, if it still exists.  This avoids that ":edit x" triggering a
   // "tabnext" BufUnload autocmd leaves a portal behind without a book.
   if (isCurPor && curPor != theCurPor && doesPortalExistInAnyTab(theCurPor)) {
      block_autocmds();
      goto_tab_port(theCurtab, theCurPor);
      unblock_autocmds();
   }

   // autocmds may abort script processing
   if ((flags & BFA_IGNORE_ABORT) == 0 && aborting())
      return;

   // It's possible that autocommands change curBook to the one being deleted.
   // This might cause curBook to be deleted unexpectedly.  But in some cases
   // it's OK to delete the curBook, because a new one is obtained anyway.
   // Therefore only return if curBook changed to the deleted book.
   if (book == curBook && !isCurBook)
      return;
   diffDeleteBook(book);       // Can't use 'diff' for unloaded book.
   // Remove any ownsyntax, unless exiting.
   if (curPor && curPor->book == book)
      reset_synblock(curPor);

   // No folds in an empty book.
   Portal* port;
   Tab* tp;
   FOR_ALL_TAB_PORTALS(tp, port) {
      if (port->book == book)
         clearFolding(port);
   } 

   ml_close(book, true);       // close and delete the memline/memfile
   book->mem.lineCount = 0;    // no lines in book
   if ((flags & BFA_KEEP_UNDO) == 0)
      // free the memory allocated for undo and reset all undo information
      invalidateUndoBufferAndFreeBlocks(book);
   syntax_clear(&book->syntax);       // reset syntax info
   clearPropTypes(book);
   book->flags &= ~BF_READERR;    // a read error is no longer relevant
}

//Free a book structure and the things it contains related to the book
//itself (not the file, that must have been done already).
private void
freeBook(Book* book){
   ++freeCallCountS;
   freeAttachedData(book, true);
   // b:changedtick uses an item in Book, remove it now
   dictitem_remove(book->bVars, (DictItem *)&book->changedTick, S"free book");
   unref_var_dict(book->bVars);
   remove_listeners(book);
   chaFreeBook(book);
   free_terminal(book);
   eeglFree(book->promptText);
   evFreeCallback(&book->promptCallback);
   evFreeCallback(&book->promptInterrupt);

   removeBookFromHashtable(book);

   scrRemoveAutocommsFromBook(book);

   if (autocmd_busy) {
      // Do not free the book structure while autocommands are executing,
      // it's still needed. Free it when autocmd_busy is reset.
      book->next = auPendingFreeBooksG;
      auPendingFreeBooksG = book;
   } else {
      eeglFree(book);
      if (curBook == book)
         curBook = NULL;  // make clear it's not to be used
   }
}

// Initializes b:changedtick.
private void
init_changedtick(Book* book){
   DictItem *di = (DictItem *)&book->changedTick;
   di->len = STRLEN(di->key);
   di->flags = DI_FLAGS_FIX | DI_FLAGS_RO;
   di->c.tag = VAR_NUMBER;
   di->c.lock = VAR_FIXED;
   di->c.number = 0;

   STRCPY(book->changedTick.key, "changedtick");
   book->changedTick.len = 11;
   (void)bagAdd(book->bVars, di);
}

// Free the portInfos list for book
private void
clearPortInfo(Book* book){
   while (book->portInfos) {
      PortInfo* poInfo = book->portInfos;
      book->portInfos = poInfo->next;
      free_wininfo(poInfo);
   }
}

// Free stuff in the book for ":bdel" or when wiping out the book.
private void
freeAttachedData(Book* book, int free_options) {     // free options as well
   if (free_options) {
      clearPortInfo(book);      // including portal-local options
      optFreeBookCallbacks(book);
      ga_clear(&book->syntax.b_langp);
   }
   {
      Long tick = CHANGEDTICK(book);

      vars_clear(&book->bVars->hashTable); // free all book variables
      hash_init(&book->bVars->hashTable);
      init_changedtick(book);
      CHANGEDTICK(book) = tick;
      remove_listeners(book);
   }
   uc_clear(&book->userCommands);      // clear local user commands
   llDeleteSigns(book, S"*");   // delete any signs
   ga_clear_strings(&book->textPropText);
   mapClearAllMappingsInMode(book, MAP_ALL_MODES, true, false);  // clear local mappings
   mapClearAllMappingsInMode(book, MAP_ALL_MODES, true, true);   // clear local abbrevs
}

// Free one PortInfo.
pub void
free_wininfo(PortInfo *poInfo) {
   if (poInfo->isOptChanged) {
      optClearPortOptions(&poInfo->opt);
      deleteFoldRecurse(&poInfo->folds);
   }
   eeglFree(poInfo);
}

// Go to another book. Handles the result of the ATTENTION dialog.
pub void
bookGoto(Invocation* invo, int start, int dir, int count){
   BookRef oldCurBook;
   int save_sea = swap_exists_action;
   int skipHelpAndQuickfix;

   switch (invo->id) {
   case C_bnext:
   case C_sbnext:
   case C_bNext:
   case C_bprevious:
   case C_sbNext:
   case C_sbprevious:
      skipHelpAndQuickfix = true;
      break;
   default:
      skipHelpAndQuickfix = false;
      break;
   }

   bookStoreInRef(OUT &oldCurBook, curBook);

   if (swap_exists_action == SEA_NONE)
      swap_exists_action = SEA_DIALOG;
   (void)bookDo(
      *invo->comm == 's' ? DOBOOK_SPLIT : DOBOOK_GOTO, start, dir, count,
      (invo->forceit ? DOBOOK_FORCEIT : 0) | (skipHelpAndQuickfix ? DOBOOK_SKIPHELP : 0)
   );
   if (swap_exists_action == SEA_QUIT && *invo->comm == 's') {
      Cleanup   cs;

      // Reset the error/interrupt/exception state here so that
      // aborting() returns false when closing a portal.
      enter_cleanup(&cs);

      // Quitting means closing the split portal, nothing else.
      closePortal(curPor, true);
      swap_exists_action = save_sea;
      swap_exists_did_quit = true;

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
   } else
      handle_swap_exists(&oldCurBook);
}

//Handle the situation of swap_exists_action being set.
//It is allowed for "oldCurBook" to be NULL or invalid.
pub void
handle_swap_exists(BookRef *oldCurBook) {
   Cleanup   cs;
   Book   *book;

   if (swap_exists_action == SEA_QUIT) {
      // Reset the error/interrupt/exception state here so that
      // aborting() returns false when closing a book.
      enter_cleanup(&cs);

      // User selected Quit at ATTENTION prompt.  Go back to previous
      // book. If that book is gone or the same as the current one, open a new, empty book.
      swap_exists_action = SEA_NONE;   // don't want it again
      swap_exists_did_quit = true;
      bookClose(curPor, curBook, DOBOOK_UNLOAD, false, false);
      if (!oldCurBook || !bookRefValid(oldCurBook) || oldCurBook->c == curBook) {
         // Block autocommands here because curPor->book is NULL.
         block_autocmds();
         book = bookNew(NULL, NULL, 1L, BLN_CURBOOK | BLN_LISTED);
         unblock_autocmds();
      } else
         book = oldCurBook->c;
      if (book) {
         int old_msg_silent = msg_silent;

         enterBook(book);
         // restore msg_silent, so that the command line will be shown
         msg_silent = old_msg_silent;
      }
      // If "oldCurBook" is NULL we are in big trouble here...

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
   } ei (swap_exists_action == SEA_RECOVER) {
      //Reset the error/interrupt/exception state here so that
      //aborting() returns false when closing a book.
      enter_cleanup(&cs);

      //User selected Recover at ATTENTION prompt.
      msg_scroll = true;
      ml_recover(false);
      msg_puts(S"\n");   // don't overwrite the last message
      commlineRowG = msgRowG;

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
   }
   swap_exists_action = SEA_NONE;
}

// Make the current book empty. Used when it is wiped out and it's the last book.
private int
emptyCurBook(int portCloseOthers, Boole forceit, Unt action) {
   int retval;
   Book* book = curBook;
   BookRef bookRef;

   if (action == DOBOOK_UNLOAD) {
      emsg(_(e_cannot_unload_last_buffer));
      return FAIL;
   }

   bookStoreInRef(OUT &bookRef, book);
   if (portCloseOthers)
      // Close any other portals into this book, then make it empty.
      closePortalsInto(book, true);

   setpcmark();
   retval = startEditingFile(0, NULL, NULL, NULL, ECMD_ONE, forceit ? ECMD_FORCEIT : 0, curPor);

   // startEditingFile() may create a new book, then we have to delete the old one. But 
   // startEditingFile() may have done that already, check if the book still exists.
   if (book != curBook && bookRefValid(&bookRef) && book->countPortals == 0)
      bookClose(NULL, book, action, false, false);
   if (!portCloseOthers)
      needFileinfoG = false;
   return retval;
}

//Implementation of the commands for the book list.
//
//action == DOBOOK_GOTO       go to specified book
//action == DOBOOK_SPLIT    split portal and go to specified book
//action == DOBOOK_UNLOAD   unload specified book(s)
//action == DOBOOK_DEL       delete specified book(s) from book list
//action == DOBOOK_WIPE       delete specified book(s) really
//action == DOBOOK_WIPE_REUSE idem, and add number to "recycledFileNumberS"
//
//start == DOBOOK_CURRENT   go to "count" book from current book
//start == DOBOOK_FIRST       go to "count" book from first book
//start == DOBOOK_LAST       go to "count" book from last book
//start == DOBOOK_MOD       go to "count" modified book from current book
//
//Return FAIL or OK.
pub int
bookDo(
   Unt action,
   Unt start,
   Unt dir,      // FORWARD or BACKWARD
   int count,      // book number
   Unt flags   // DOBOOK_FORCEIT when using !, etc
){
   Book* book;
   int unload = (action == DOBOOK_UNLOAD || action == DOBOOK_DEL
            || action == DOBOOK_WIPE || action == DOBOOK_WIPE_REUSE);
   switch (start) {
   case DOBOOK_FIRST:   book = firstBook; break;
   case DOBOOK_LAST:    book = lastBook;  break;
   default:       book = curBook;   break;
   }
   
   if (start == DOBOOK_MOD) {      // find next modified book
      while (count-- > 0) {
         do {
            book = book->next ? book->next : firstBook;
         }
         while (book != curBook && !bookWasChanged(book));
      }
      if (!bookWasChanged(book)) {
          emsg(_(e_no_modified_buffer_found));
          return FAIL;
      }
   } ei (start == DOBOOK_FIRST && count) { // find specified book number
      while (book && book->fiNum != count)
         book = book->next;
   } else {
      int helpOnly = (flags & DOBOOK_SKIPHELP) != 0 && book->kind == BOOK_HELP;

      Book* bp = NULL;
      while (count > 0 
            || (bp != book && !unload && !(helpOnly ? book->kind == BOOK_HELP : book->o.bookListed))
      ) {
         // remember the book where we start, we return there when all books are unlisted
         if (bp == NULL)
            bp = book;
         if (dir == FORWARD) {
            book = book->next;
            if (book == NULL)
               book = firstBook;
         } else {
            book = book->prev;
            if (book == NULL)
               book = lastBook;
         }
         // Avoid non-help books if the starting point was a help book and vice-versa.
         // Don't count unlisted books.
         if (unload
                || (helpOnly
                  ? book->kind == BOOK_HELP
                  : (book->o.bookListed 
                     && ((flags & DOBOOK_SKIPHELP) == 0 
                        || (book->kind != BOOK_HELP && book->kind != BOOK_LOCATION) 
                        )
                  )
               )
         ) {
            --count;
            bp = NULL; // use this book as new starting point
         }
         if (bp == book) {
            // back where we started, didn't find anything.
            emsg(_(e_there_is_no_listed_buffer));
            return FAIL;
         }
      }
   }

   if (book == NULL) {    // could not find it
      if (start == DOBOOK_FIRST) {
         // don't warn when deleting
         if (!unload)
            showErrFmtMsg(_(e_book_nr_does_not_exist), count);
      } ei (dir == FORWARD)
         emsg(_(e_cannot_go_beyond_last_buffer));
      else
         emsg(_(e_cannot_go_before_first_buffer));
      return FAIL;
   }
   if ((flags & DOBOOK_NOPOPUP) && bt_popup(book) && !bt_terminal(book))
      return OK;
   if (action == DOBOOK_GOTO && book != curBook) {
      if (!portCheckCanSetCurBookForceIt((flags & DOBOOK_FORCEIT) != 0))
         // disallow navigating to another book when 'portfixbuf' is applied
         return FAIL;
      if (book->lockedSplit) {
         // disallow navigating to a closing book, which like splitting,
         // can result in more portals displaying it
         emsg(_(e_cannot_switch_to_a_closing_buffer));
         return FAIL;
      }
   }

   if ((action == DOBOOK_GOTO || action == DOBOOK_SPLIT) && (book->flags & BF_DUMMY)) {
      // disallow navigating to the dummy book
      showErrFmtMsg(_(e_book_nr_does_not_exist), count);
      return FAIL;
   }

   // delete "book" from memory and/or the list
   if (unload) {
      int forward;
      BookRef bookRef;

      if (!canUnloadBook(book))
         return FAIL;

      bookStoreInRef(OUT &bookRef, book);

      // When unloading or deleting a book that's already unloaded and
      // unlisted: fail silently.
      if (action != DOBOOK_WIPE && action != DOBOOK_WIPE_REUSE
                  && !book->mem.mfile && !book->o.bookListed)
          return FAIL;

      if ((flags & DOBOOK_FORCEIT) == 0 && bookWasChanged(book)) {
         if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
            if (term_job_running(book->term)) {
               if (term_confirm_stop(book) == FAIL)
                  return FAIL;
            } else {
               dialog_changed(book, false);
               if (!bookRefValid(&bookRef))
                  // Autocommand deleted book, oops!  It's not changed now.
                  return FAIL;
               // If it's still changed fail silently, the dialog already mentioned why it fails.
               if (bookWasChanged(book))
                  return FAIL;
            }
         } else {
            no_write_message_buf(book);
            return FAIL;
         }
      }

      // When closing the current book stop Visual mode.
      if (book == curBook && VIsual_active)
         end_visual_mode();

      //If deleting the last (listed) book, make it empty. The last (listed) book cannot be unloaded
      Book* bp;
      FOR_ALL_BOOKS(bp) {
         if (bp->o.bookListed && bp != book)
            break;
      } 
      if (!bp && book == curBook)
         return emptyCurBook(true, (flags & DOBOOK_FORCEIT) != 0, action);

      // If the deleted book is the current one, close the current portal (unless it's the only 
      // portal). Repeat this so long as we end up in a portal with this book.
      while (book == curBook
            && !(portalLocked(curPor) || curPor->book->locked > 0)
            && (!ONLY_ONE_PORTAL || firstTabG->next)
      ) {
         if (closePortal(curPor, false) == FAIL)
            break;
      }

      // If the book to be deleted is not the current one, delete it here.
      if (book != curBook) {
         closePortalsInto(book, false);
         if (book != curBook && bookRefValid(&bookRef) && book->countPortals <= 0)
            bookClose(NULL, book, action, false, false);
         return OK;
      }

      //Deleting the current book: Need to find another book to go to. There should be another, 
      //otherwise it would have been handled above. However, autocommands may have deleted all 
      //books. First use auNewCurBook.c, if it is valid. Then prefer the book we most recently 
      //visited. Else try to find one that is loaded, after the current book, then before the 
      //current book. Finally use any book.
      book = NULL;  // selected book
      bp = NULL;   // used when no loaded book found
      if (auNewCurBookG.c && bookRefValid(&auNewCurBookG))
          book = auNewCurBookG.c;
      ei (curPor->jumpListLen > 0) {
          int     jumpidx;

         jumpidx = curPor->jumpListInd - 1;
         if (jumpidx < 0)
            jumpidx = curPor->jumpListLen - 1;

         forward = jumpidx;
         while (jumpidx != curPor->jumpListInd) {
            book = bookFindFileByBookNr(curPor->jumpList[jumpidx].fmark.fnum);
            if (book) {
               //Skip current and unlisted bufs. Also skip a location book, it might be deleted soon
               if (book == curBook || !book->o.bookListed || isLocationListBook(book))
                  book = NULL;
               ei (book->mem.mfile == NULL) {
                  // skip unloaded book, but may keep it for later
                  if (!bp)
                     bp = book;
                  book = NULL;
               }
            }
            if (book)   // found a valid book: stop searching
               break;
            // advance to older entry in jump list
            if (!jumpidx && curPor->jumpListInd == curPor->jumpListLen)
               break;
            if (--jumpidx < 0)
               jumpidx = curPor->jumpListLen - 1;
            if (jumpidx == forward)      // List exhausted for sure
               break;
         }
      }

      if (book == NULL) { // No previous book, Try 2'nd approach
         forward = true;
         book = curBook->next;
         for (;;) {
            if (book == NULL) {
               if (!forward)   // tried both directions
                  break;
               book = curBook->prev;
               forward = false;
               continue;
            }
            // in non-help book, try to skip help books, and vv
            if ((book->kind == BOOK_HELP) == (curBook->kind == BOOK_HELP) && book->o.bookListed
                   && !isLocationListBook(book)
            ){
               if (book->mem.mfile)   // found loaded book
                  break;
               if (!bp)   // remember unloaded book for later
                  bp = book;
            }
            if (forward)
               book = book->next;
            else
               book = book->prev;
         }
      }
      if (!book)   // No loaded book, use unloaded one
          book = bp;
      if (!book) {  // No loaded book, find listed one
         FOR_ALL_BOOKS(book) {
            if (book->o.bookListed && book != curBook && !isLocationListBook(book))
               break;
         } 
      }
      if (!book) {  // Still no book, just take one
         if (curBook->next)
            book = curBook->next;
         else
            book = curBook->prev;
         if (isLocationListBook(book))
            book = NULL;
      }
   }

   if (!book) {
      // Autocommands must have wiped out all other books. Only option
      // now is to make the current book empty.
      return emptyCurBook(false, (flags & DOBOOK_FORCEIT) != 0, action);
   }

   // make "book" the current book
   if (action == DOBOOK_SPLIT) {     // split portal first
      // If 'switchbook' is set jump to the portal containing "book".
      if (switchBufGotoPortalIntoBuf(book) != NULL)
         return OK;

      if (splitPortal(0, 0) == FAIL)
         return FAIL;
   }

   // go to current book - nothing to do
   if (book == curBook)
      return OK;

   // Go to the other book.
   bookSetCurBook(book, action);

   if (action == DOBOOK_SPLIT)
      curPor->o.diff = false;   // disable scrollbinding and cursorbinding

   if (aborting())       // autocmds may abort script processing
      return FAIL;

   return OK;
}

//do_bufdel() - delete or unload book(s)
//
//addr_count == 0: ":bdel" - delete current book
//addr_count == 1: ":N bdel" or ":bdel N [N ..]" - first delete
//         book "end_bnr", then any other arguments.
//addr_count == 2: ":N,N bdel" - delete books in range
//
//command can be DOBOOK_UNLOAD (":bunload"), DOBOOK_WIPE (":bwipeout") or DOBOOK_DEL (":bdel")
//
//Return error message or NULL
pub CS
do_bufdel(
   int command,
   CS arg,      // pointer to extra arguments
   int addr_count,
   int start_bnr,   // first book number in a range
   int end_bnr,   // book nr or last book nr in a range
   Boole forceit
) {
   int do_current = 0;   // delete current book?
   int deleted = 0;   // number of books deleted
   CS errormsg = NULL; // return value
   int bnr;      // book number
   CS p;

   if (addr_count == 0) {
      (void)bookDo(command, DOBOOK_CURRENT, FORWARD, 0, forceit);
   } else {
      if (addr_count == 2) {
         if (*arg)      // both range and argument is not allowed
            return ex_errmsg(e_trailing_characters_str, arg);
         bnr = start_bnr;
      } else   // addr_count == 1
         bnr = end_bnr;

      for ( ;!gotInterruptG; ui_breakcheck()) {
         //Delete the current book last, otherwise when the current book is deleted, the next 
         //book becomes the current one and will be loaded, which may then also be deleted, etc.
         if (bnr == curBook->fiNum)
            do_current = bnr;
         ei (bookDo(
               command, DOBOOK_FIRST, FORWARD, bnr, DOBOOK_NOPOPUP | (forceit ? DOBOOK_FORCEIT : 0)
            ) == OK
         )
         ++deleted;

         // find next book number to delete/unload
         if (addr_count == 2) {
            if (++bnr > end_bnr)
               break;
         } else  {   // addr_count == 1
            arg = skipwhite(arg);
            if (*arg == ZERO)
               break;
            if (!EE_ISDIGIT(*arg)) {
               p = skiptowhite_esc(arg);
               bnr = booklistFindPattern(
                      arg, p, command == DOBOOK_WIPE || command == DOBOOK_WIPE_REUSE, false, false
               );
               if (bnr < 0)       // failed
                  break;
               arg = p;
            } else
               bnr = parseLong(&arg);
         }
      }
      if (!gotInterruptG && do_current && bookDo(command, DOBOOK_FIRST,
                    FORWARD, do_current, forceit) == OK)
          ++deleted;

      if (deleted == 0) {
         if (command == DOBOOK_UNLOAD)
            STRCPY(IObuff, _(e_no_buffers_were_unloaded));
         ei (command == DOBOOK_DEL)
            STRCPY(IObuff, _(e_no_buffers_were_deleted));
         else
            STRCPY(IObuff, _(e_no_buffers_were_wiped_out));
         errormsg = IObuff;
      } else {
         if (command == DOBOOK_UNLOAD)
            smsg(NGETTEXT("%d book unloaded", "%d books unloaded", deleted), deleted);
         ei (command == DOBOOK_DEL)
            smsg(NGETTEXT("%d book deleted", "%d books deleted", deleted), deleted);
         else
            smsg(NGETTEXT("%d book wiped out", "%d books wiped out", deleted), deleted);
      }
   }

   return errormsg;
}

//Set current book to "book". Execute autocommands and close current book.  
//"action" tells how to close the current book:
//DOBOOK_GOTO       free or hide it
//DOBOOK_SPLIT      nothing
//DOBOOK_UNLOAD     unload it
//DOBOOK_DEL        delete it
//DOBOOK_WIPE       wipe it out
//DOBOOK_WIPE_REUSE wipe it out and add to "recycledFileNumberS"
pub void
bookSetCurBook(Book* book, int action) {
   int unload = (action == DOBOOK_UNLOAD || action == DOBOOK_DEL
         || action == DOBOOK_WIPE || action == DOBOOK_WIPE_REUSE);
   BookRef   newbufref;
   BookRef   prevbufref;
   int      valid;

   setpcmark();
   if ((commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
      curPor->altFnum = curBook->fiNum; // remember alternate file
      
   bookSetPosInPort(curBook, curPor, curPor->cursor.lnum, curPor->cursor.col, true);

   // Don't restart Select mode after switching to another book.
   VIsual_reselect = false;

   // closePortalsInto() or applyAutocomms() may change curBook and wipe out "book"
   Book* prevbuf = curBook;
   bookStoreInRef(OUT &prevbufref, prevbuf);
   bookStoreInRef(OUT &newbufref, book);

   // Autocommands may delete the current book and/or the book we want to
   // go to.  In those cases don't close the book.
   if (!applyAutocomms(EVENT_BUFLEAVE, NULL, NULL, false, curBook)
       || (bookRefValid(&prevbufref)
            && bookRefValid(&newbufref)
            && !aborting()
          )
   ) {
      if (prevbuf == curPor->book)
         reset_synblock(curPor);
      // autocommands may have opened a new portal with prevbuf, grr
      if (unload)
         closePortalsInto(prevbuf, false);
      if (bookRefValid(&prevbufref) && !aborting()) {
         Portal  *previouswin = curPor;

         // Do not sync when in Insert mode and there is another portal into the book, might 
         // be a timer doing something in another portal.
         if (prevbuf == curBook
                && ((stateG & MODE_INSERT) == 0 || curBook->countPortals <= 1)
         )
            u_sync(false);
         bookClose(
            prevbuf == curPor->book ? curPor : NULL, prevbuf,
            unload ? action : 0,
            false, false
         );
         if (curPor != previouswin && portalIsValid(previouswin))
            // autocommands changed curPor, Grr!
            curPor = previouswin;
      }
   }
   // An autocommand may have deleted "book", already entered it (e.g., when
   // it did ":bunload") or aborted the script processing.
   // If curPor->book is null, enterBook() will make it valid again
   valid = bookIsValid(book);
   if ((valid && book != curBook && !aborting()) || curPor->book == NULL) {
      // autocommands changed curBook and we will move to another
      // book soon, so decrement curBook->countPortals
      if (curBook && prevbuf != curBook)
         curBook->countPortals--;
      // If the book is not valid but curPor->book is NULL we must
      // enter some book.  Using the last one is hopefully OK.
      if (!valid) {
         enterBook(lastBook);
      } else {
         enterBook(book);
      } 
   }
}

//Enter a new current book. Old curBook must have been abandoned already! This also means 
//"curBook" may be pointing to freed memory.
private void
enterBook(Book* book){
   //when closing the current book, stop Visual mode
   if (VIsual_active
#if defined(EXITFREE)
       && !entered_free_all_mem
#endif
   )
      end_visual_mode();

   //Get the book in the current portal.
   curPor->book = book;
   curBook = book;
   ++curBook->countPortals;

   //Copy book and portal local option values. Not for a help book.
   optsCopyToBook(book, BCO_ENTER);
   if (book->kind != BOOK_HELP)
      get_winopts(book);
   else
      // Remove all folds in the portal.
      clearFolding(curPor);
   foldUpdateAll(curPor);   // update folds (later).

   if (curPor->o.diff)
      diffAddBook(curBook);

   curPor->ownSyntax = &(curBook->syntax);

   // Cursor on first line by default.
   curPor->cursor.lnum = 1;
   curPor->cursor.col = 0;
   curPor->cursor.coladd = 0;
   curPor->setCursWant = true;
   curPor->wasTopLineSet = false;

   // mark cursor position as being invalid
   curPor->cacheState = 0;

   // Make sure the book is loaded.
   if (curBook->mem.mfile == NULL) {  // need to load the file
      //If there is no filetype, allow for detecting one.  Esp. useful for ":ball" used in an 
      //autocommand. If there already is a filetype we might prefer to keep it.
      if (!curBook->fileType)
         curBook->didFiletype = false;

      bookOpenFromInvo(false, NULL, 0);
   } else {
      if (!msg_silent)
         needFileinfoG = true;   // display file info after redraw

      // check if file changed
      (void)bookCheckTimestamp(curBook);

      curPor->topLine = 1;
      curPor->topFill = 0;
      applyAutocomms(EVENT_BUFENTER, NULL, NULL, false, curBook);
      applyAutocomms(EVENT_BUFWINENTER, NULL, NULL, false, curBook);
    }

   //If autocommands did not change the cursor position, restore cursor lnum
   //and possibly cursor col.
   if (curPor->cursor.lnum == 1 && inindent(0))
      getLastKnownLineNumber();

   check_arg_idx(curPor);      // check for valid arg_idx
   // when autocmds didn't change it
   if (curPor->topLine == 1 && !curPor->wasTopLineSet)
      scroll_cursor_halfway(false, false);   // redisplay at correct position

   // Change directories when the 'acd' option is set.
   DO_AUTOCHDIR;

   curBook->lastUsed = eeTime();
   redraw_later(UPD_NOT_VALID);
}

private void
no_write_message_buf(Book* book) {
   if (term_job_running(book->term))
      emsg(_(e_job_still_running_add_bang_to_end_the_job));
   else
      showErrFmtMsg(_(e_no_write_since_last_change_for_buffer_nr_add_bang_to_override), book->fiNum);
}

pub void
no_write_message(void) {
   if (term_job_running(curBook->term))
      emsg(_(e_job_still_running_add_bang_to_end_the_job));
   else
      emsg(_(e_no_write_since_last_change_add_bang_to_override));
}

pub void
no_write_message_nobang(Book* book) {
   if (term_job_running(book->term))
      emsg(_(e_job_still_running));
   else
      emsg(_(e_no_write_since_last_change));
}

// functions for dealing with the book list

// Return true if the current book is empty, unnamed, unmodified and used in
// only one portal. That means it can be re-used.
private Boole
isCurBookReusable(void) {
   return (curBook
      && !curBook->fullFileName
      && curBook->countPortals <= 1
      && (!curBook->mem.mfile || CURBOOK_EMPTY())
      && !isLocationListBook(curBook)
      && !bookWasChanged(curBook)
   );
}

//Add a file name to the book list. Return a pointer to the book.
//If the same file name already exists, return a pointer to that book.
//If it does not exist, or if fname == NULL, a new entry is created.
//If (flags & BLN_CURBOOK) is true, may use current book.
//If (flags & BLN_LISTED) is true, add new book to book list.
//If (flags & BLN_DUMMY) is true, don't count it as a real book.
//If (flags & BLN_NEW) is true, don't use an existing book.
//If (flags & BLN_NOOPT), don't copy options from the current book if the book already exists.
//If (flags & BLN_REUSE) is true, may use book number from "recycledFileNumberS".
//This is the ONLY way to create a new book.
pub Book*
bookNew(
   CS ffname_arg, // full path of fname or relative
   CS sfname_arg, // short fname or NULL
   LineNr lnum,   // preferred cursor line
   Unt flags
) {                    // BLN_ defines
   CS fullFName = ffname_arg;
   CS sfname = sfname_arg;

   if (top_file_num == 1)
      hash_init(&buf_hashtab);

   fname_expand(&fullFName, &sfname);   // will allocate fullFName

   //If the file name already exists in the list, update the entry. On Unix we can use inode 
   //numbers when the file exists. Works better for hard links.
   FileStat st;
   if (!sfname || stat((char *)sfname, &st) < 0)
      st.st_dev = (Device)-1;
      
   Book* book = null;
   // found existing book with this file
   if (fullFName && (flags & (BLN_DUMMY | BLN_NEW)) == 0 
         && (book = booklistFindName_stat(fullFName, &st)) != NULL
   ) {
      eeglFree(fullFName);
      if (lnum != 0)
         bookSetPosInPort(book, (flags & BLN_NOCURWIN) ? NULL : curPor, lnum, (ColNr)0, false);

      if ((flags & BLN_NOOPT) == 0)
         optsCopyToBook(book, 0);

      if ((flags & BLN_LISTED) && !book->o.bookListed) {
         BookRef bookRef;
         book->o.bookListed = true;
         bookStoreInRef(OUT &bookRef, book);
         if (!(flags & BLN_DUMMY)) {
            if (applyAutocomms(EVENT_BUFADD, NULL, NULL, false, book) && !bookRefValid(&bookRef))
               return NULL;
         }
      }
      return book;
   }

   //If the current book has no name and no contents, use it.
   //Otherwise: Need to allocate a new book structure.
   //
   //This is the ONLY place where a new book structure is allocated!
   if ((flags & BLN_CURBOOK) != 0 && isCurBookReusable()) {
      BookRef bookRef;
      book = curBook;
      
      bookStoreInRef(OUT &bookRef, book);
      //It's like this book is deleted. Watch out for autocommands that
      //change curBook! If that happens, allocate a new book anyway.
      bookFreeAll(book, BFA_WIPE | BFA_DEL);
      if (aborting()) {    // autocmds may abort script processing
         eeglFree(fullFName);
         return NULL;
      }
      if (!bookRefValid(&bookRef))
         book = NULL;      // book was deleted; allocate a new book
   }
   if (book != curBook || !curBook) {
      book = ALLOC_CLEAR_ONE(Book);
      
      // init b: variables
      book->bVars = allocBag_id(aid_newbuf_bvars);
      if (!book->bVars) {
         eeglFree(fullFName);
         eeglFree(book);
         return NULL;
      }
      init_var_dict(book->bVars, &book->bookVar, VAR_SCOPE);
      init_changedtick(book);
   }

   if (fullFName) {
      book->fullFileName = fullFName;
      book->shortFileName = copyStr(sfname);
   }
        
   clearPortInfo(book);
   book->portInfos = ALLOC_CLEAR_ONE(PortInfo);

   if (fullFName && (!book->fullFileName || !book->shortFileName)) {
      if (book->shortFileName != book->fullFileName)
         EE_CLEAR(book->shortFileName);
      else
         book->shortFileName = NULL;
      EE_CLEAR(book->fullFileName);
      if (book != curBook)
         freeBook(book);
      return NULL;
   }

   if (book == curBook) {
      freeAttachedData(book, false);   // delete local variables et al.

      // Init the options.
      book->o.initialized = false;
      book->o.modifiable = (flags & BLN_MODIFIABLE) != 0;
      optsCopyToBook(book, BCO_ENTER);
   } else {
      // put the new book at the end of the book list
      book->next = NULL;
      
      if (!firstBook) {     // book list is empty
         book->prev = NULL;
         firstBook = book;
      } else {        // append new book at end of list
         lastBook->next = book;
         book->prev = lastBook;
      }
      lastBook = book;

      if ((flags & BLN_REUSE) != 0 && recycledFileNumberS.len > 0) {
         // Recycle a previously used book number. Used for books which
         // are normally hidden, e.g. in a popup portal. Avoids that the book number grows rapidly
         --recycledFileNumberS.len;
         book->fiNum = ((int *)recycledFileNumberS.c)[recycledFileNumberS.len];

         // Move book to the right place in the book list.
         while (book->prev && book->fiNum < book->prev->fiNum) {
            Book* prev = book->prev;

            prev->next = book->next;
            if (prev->next)
               prev->next->prev = prev;
            book->next = prev;
            book->prev = prev->prev;
            if (book->prev)
               book->prev->next = book;
            prev->prev = book;
            if (lastBook == book)
               lastBook = prev;
            if (firstBook == prev)
               firstBook = book;
         }
      } else
         book->fiNum = top_file_num++;
      if (top_file_num < 0) {     // wrap around (may cause duplicates)
         emsg(_("W14: Warning: List of file names overflow"));
         if (emsg_silent == 0 && !in_assert_fails) {
            out_flush();
            ui_delay(3001L, true);   // make sure it is noticed
         }
         top_file_num = 1;
      }
      addBookToHashtable(book);

      // Always copy the options from the current book.
      book->o.modifiable = flags & BLN_MODIFIABLE;
      optsCopyToBook(book, BCO_ALWAYS);
   }

   book->portInfos->wi_fpos.lnum = lnum;
   book->portInfos->portal = curPor;

   hash_init(&book->syntax.keywords);
   hash_init(&book->syntax.keywordsIgnoreCase);

   book->currFileName = book->shortFileName;
   if (st.st_dev == (Device) - 1)
      book->isDevNumValid = false;
   else {
      book->isDevNumValid = true;
      book->devNum = st.st_dev;
      book->inode = st.st_ino;
   }
   book->undo.synced = true;
   book->flags = BF_CHECK_RO | BF_NEVERLOADED;
   if (flags & BLN_DUMMY)
      book->flags |= BF_DUMMY;
   buf_clear_file(book);
   clrallmarks(book);         // clear marks
   
   fmarks_check_names(book);      // check file marks for this file
   book->o.bookListed = (flags & BLN_LISTED) ? true : false;   // init 'buflisted'
   if (!(flags & BLN_DUMMY)) {
      //Tricky: these autocommands may change the book list. They could also split the portal
      //with re-using the one empty book. This may result in unexpectedly losing the empty book
      BookRef bookRef;
      bookStoreInRef(OUT &bookRef, book);
      if (applyAutocomms(EVENT_BUFNEW, NULL, NULL, false, book) && !bookRefValid(&bookRef))
         return NULL;
      if ((flags & BLN_LISTED) != 0) {
         if (applyAutocomms(EVENT_BUFADD, NULL, NULL, false, book) && !bookRefValid(&bookRef))
            return NULL;
      }
      if (aborting())      // autocmds may abort script processing
          return NULL;
   }
      
   return book;
}

//Get alternate file "n".
//Set linenr to "lnum" or altfpos.lnum if "lnum" == 0.
//Also set cursor column to altfpos.col if 'startofline' is not set.
//if (options & GETF_SETMARK) call setpcmark()
//if (options & GETF_ALT) we are jumping to an alternate file.
//if (options & GETF_SWITCH) respect 'switchbook' settings when jumping
//
//Return FAIL for failure, OK for success.
pub int
booklistGetFile(
   int n,
   LineNr   lnum,
   int options,
   int forceit
){
   Portal* po = NULL;
   Pos* fpos;
   ColNr col;

   Book* book = bookFindFileByBookNr(n);
   if (!book) {
      if ((options & GETF_ALT) && n == 0)
         emsg(_(e_no_alternate_file));
      else
         showErrFmtMsg(_(e_book_nr_not_found), n);
      return FAIL;
   }

   // if alternate file is the current book, nothing to do
   if (book == curBook)
      return OK;

   if (text_or_buf_locked())
      return FAIL;

   // altfpos may be changed by getfile(), get it now
   if (lnum == 0) {
      fpos = bookFindFpos(book);
      lnum = fpos->lnum;
      col = fpos->col;
   } else
      col = 0;

   if ((options & GETF_SWITCH) != 0) {
      // If @switchbook is set, jump to the portal containing "book".
      po = switchBufGotoPortalIntoBuf(book);

      //If @switchbook contains "split", "vsplit" or "newtab" and the
      //current book isn't empty: open new tab or portal
      if (!po && (p_swb & (SWB_VSPLIT | SWB_SPLIT | SWB_NEWTAB)) != 0 && !CURBOOK_EMPTY()) {
         if ((p_swb & SWB_NEWTAB) != 0)
            tabNew();
         ei (splitPortal(0, (p_swb & SWB_VSPLIT) ? WSP_VERT : 0) == FAIL)
            return FAIL;
         curPor->o.diff = false;
      }
   }

   ++isRedrawingDisabledG;
   int retval = FAIL;
   if (GETFILE_SUCCESS(getfile(book->fiNum, NULL, NULL, (options & GETF_SETMARK), lnum, forceit))) {
      // cursor is at to BOL and cursor.lnum is checked due to getfile()
      if (!p_sol && col != 0) {
         curPor->cursor.col = col;
         check_cursor_col();
         curPor->cursor.coladd = 0;
         curPor->setCursWant = true;
      }
      retval = OK;
   }

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   return retval;
}

// Go to the last known line number for the current book
private void
getLastKnownLineNumber(void) {
   Pos* fpos = bookFindFpos(curBook);

   curPor->cursor.lnum = fpos->lnum;
   check_cursor_lnum();

   if (p_sol)
      curPor->cursor.col = 0;
   else {
      curPor->cursor.col = fpos->col;
      check_cursor_col();
      curPor->cursor.coladd = 0;
      curPor->setCursWant = true;
   }
}

// Find file in book list by name. Return NULL if not found.
pub Book *
booklistFindByNameExpandingLinks(CS fname) {
   // First make the name into a full path name
   CS fullFName = fiExpandAndCopy(fname, true);      // force expansion, get rid of symbolic links
   Book* book = NULL;
   if (fullFName) {
      book = booklistFindName(fullFName);
      eeglFree(fullFName);
   }
   return book;
}

//Find file in book list by name. "fullFName" must have a full path.
//Skip dummy books. Return NULL if not found.
pub Book*
booklistFindName(CS fullFName){
   FileStat st;
   if (stat((char *)fullFName, &st) < 0)
      st.st_dev = (Device)-1;
   return booklistFindName_stat(fullFName, &st);
}

private Boole
sameFileInBook(Book* book, CS fullFName, FileStat* stp) {
   // no name is different
   if (fullFName == NULL || *fullFName == ZERO || book->fullFileName == NULL)
      return false;
   if (fnamecmp(fullFName, book->fullFileName) == 0)
      return true;

   FileStat st;
   // If no FileStat given, get it now
   if (stp == NULL) {
      if (!book->isDevNumValid || stat((char *)fullFName, &st) < 0)
         st.st_dev = (Device)-1;
      stp = &st;
   }
   // Use dev/ino to check if the files are the same, even when the names are different (possible 
   // with links).  Still need to compare the name above, for when the file doesn't exist yet.
   // Problem: The dev/ino changes when a file is deleted (and created again) and remains the same 
   // when renamed/moved.  We don't want to stat() each book each time, that would be too 
   // slow.  Get the dev/ino again when they appear to match, but not when they appear to be 
   // different: Could skip a book when it's actually the same file.
   if (areSameInode(book, stp)) {
      buf_setino(book);
      if (areSameInode(book, stp))
         return true;
   }
   return false;
}


// Find file in book list by name, but pass the stat structure to avoid getting it twice for 
// the same file. Return NULL if not found.
private Book*
booklistFindName_stat(CS fullFName, FileStat* stp) {
   // Start at the last book, expect to find a match sooner.
   Book* book;
   FOR_ALL_BOOKS_FROM_LAST(book) {
      if ((book->flags & BF_DUMMY) == 0 && sameFileInBook(book, fullFName, stp))
         return book;
   } 
   return NULL;
}

//Find file in book list by a regexp pattern.
//Return fnum of the found book. Return < 0 for error.
pub int
booklistFindPattern(
   CS pattern,
   CS pattern_end,   // pointer to first char after pattern
   int unlisted,   // find unlisted books
   int diffmode, // find diff-mode books only
   int curtab_only  // find books in current tab only
){
   Book* book;
   int match = -1;
   int find_listed;
   CS p;

   // "%" is current file, "%%" or "#" is alternate file
   if ((pattern_end == pattern + 1 && (*pattern == '%' || *pattern == '#'))) {
      if (*pattern == '#' || pattern_end == pattern + 2)
         match = curPor->altFnum;
      else
         match = curBook->fiNum;
      if (diffmode && !diffIsBookInDiffMode(bookFindFileByBookNr(match)))
         match = -1;
   }

   //Try four ways of matching a listed book:
   //attempt == 0: without '^' or '$' (at any position)
   //attempt == 1: with '^' at start (only at position 0)
   //attempt == 2: with '$' at end (only match at end)
   //attempt == 3: with '^' at start and '$' at end (only full match)
   //Repeat this for finding an unlisted book if there was no matching listed book.
   else {
      CS pat = file_pat_to_reg_pat(pattern, pattern_end, NULL);
      CS patend = pat + STRLEN(pat) - 1;
      Boole toggledollar = (patend > pat && *patend == '$');

      // First try finding a listed book.  If not found and "unlisted" is true, try finding an 
      // unlisted one.
      find_listed = true;
      for (;;) {
         for (int attempt = 0; attempt <= 3; ++attempt) {
            RegMatch   regmatch;

            // may add '^' and '$'
            if (toggledollar)
               *patend = (attempt < 2) ? ZERO : '$'; // add/remove '$'
            p = pat;
            if (*p == '^' && !(attempt & 1))    // add/remove '^'
               ++p;
            regmatch.regprog = compileRegexp(p, RE_MAGIC);

            FOR_ALL_BOOKS_FROM_LAST(book) {
               if (regmatch.regprog == NULL) {
                  // invalid pattern, possibly after switching engine
                  eeglFree(pat);
                  return -1;
               }
               if (book->o.bookListed == find_listed
                      && (!diffmode || diffIsBookInDiffMode(book))
                      && checkFilenameMatch(&regmatch, book) != NULL) {
                  if (curtab_only) {
                     // Ignore the match if the book is not open in the current tab.
                     Portal* po;
                     FOR_ALL_PORTALS(po) {
                        if (po->book == book)
                           break;
                     } 
                     if (!po)
                        continue;
                  }
                  if (match >= 0) {     // already found a match
                     match = -2;
                     break;
                  }
                  match = book->fiNum;   // remember first match
               }
            }

            eeRegFree(regmatch.regprog);
            if (match >= 0)         // found one match
               break;
         }

         // Only search for unlisted books if there was no match with a listed book.
         if (!unlisted || !find_listed || match != -1)
            break;
         find_listed = false;
      }

      eeglFree(pat);
   }

   if (match == -2)
      showErrFmtMsg(_(e_more_than_one_match_for_str), pattern);
   ei (match < 0)
      showErrFmtMsg(_(e_no_matching_buffer_for_str), pattern);
   return match;
}

typedef struct {
   Book* book;
   CS match;
} BufMatch;

GEN_TYPE_L(BufMatch);
GEN_create_L(rivate, BufMatch);
generic(2) GEN_add_L(rivate, BufMatch);

//Find all book names that match. For command line expansion of ":book" and ":sbook".
//Return OK if matches found, FAIL otherwise.
pub int
bufExpandBufnames(
   CS pat,
   Unt options,
   OUT ExpandMatch* matches
){
   CS p;
   CS patSaved = NULL;
   LBufMatch* bufMatches = create(L, BufMatch)(2, matches->a);
   Fuzzy fuzzy = {};
   fuzzy.a = matches->a;
   RegMatch regmatch;
   int score = 0;
   Boole to_free = false;

   if ((options & BOOK_DIFF_FILTER) != 0 && !curPor->o.diff)
      return FAIL;

   Boole doFuzzy = scrIsCommlineFuzzyCompletable(pat);

   // Make a copy of "pat" and change "^" to "\(^\|[\/]\)" (if doing regular
   // expression matching)
   if (!doFuzzy) {
      if (*pat == '^' && pat[1] != ZERO) {
         int len = (int)STRLEN(pat);
         patSaved = alloc(len);
         STRNCPY(patSaved, pat + 1, len - 1);
         patSaved[len - 1] = ZERO;
         to_free = true;
      } ei (*pat == '^')
         patSaved = S"";
      else
         patSaved = pat;
      regmatch.regprog = compileRegexp(patSaved, RE_MAGIC);
   }

   Book* book;
   FOR_ALL_BOOKS(book) {
      if (!book->o.bookListed)
         continue;
      if ((options & BOOK_DIFF_FILTER) != 0 && (book == curBook || !diffIsBookInDiffMode(book)))
         // Skip books not suitable for :diffget or :diffput completion.
         continue;

      if (doFuzzy) {
         p = NULL;
         // first try matching with the short file name
         if ((score = fuzzyMatchStr(book->shortFileName, pat)) != FUZZY_SCORE_NONE)
            p = book->shortFileName;
         // next try matching with the full path file name
         if (!p && (score = fuzzyMatchStr(book->fullFileName, pat)) != FUZZY_SCORE_NONE)
            p = book->fullFileName;
      } else {
         if (!regmatch.regprog) {
            // invalid pattern, possibly after recompiling
            if (to_free)
               eeglFree(patSaved);
            return FAIL;
         }
         p = checkFilenameMatch(&regmatch, book);
      }

      if (!p)
         continue;

      if ((options & WILD_HOME_REPLACE) != 0)
         p = home_replace_save(book, p);
      else
         p = copyStr(p);

      if (doFuzzy) {
         addFuzzyMatch((FuzzyMatch){.str = p, .score = score}, OUT &fuzzy);
      } else {
         add(OUT bufMatches, ((BufMatch){book, p}));
      }
   }
   if (!doFuzzy) {
      eeRegFree(regmatch.regprog);
      if (to_free)
         eeglFree(patSaved);
   }

   if (doFuzzy) {
      if (defuzz(OUT matches, fuzzy, false) == FAIL)
         return FAIL;
   } else {  
      if (bufMatches->len > 1)
         qsort(bufMatches->c, bufMatches->len, sizeof(BufMatch), bookCompare);
      // if the current book is first in the list, place it at the end
      if (bufMatches->c[0].book == curBook) {
         for (Unt i = 1; i < bufMatches->len; i++)
            addExpandMatch(bufMatches->c[i].match, OUT matches);
         addExpandMatch(bufMatches->c[bufMatches->len - 1].match, OUT matches);
      } else {
         for (Unt i = 0; i < bufMatches->len; i++)
            addExpandMatch(bufMatches->c[i].match, OUT matches);
      }
   }

   return (matches->len > 0 ? OK : FAIL);
}

// Check for a match on the file name for book "book" with regprog "prog".
private CS
checkFilenameMatch(RegMatch* rmp, Book* book) {
   // First try the short file name, then the long file name.
   CS match = fname_match(rmp, book->shortFileName);
   if (!match && rmp->regprog)
      match = fname_match(rmp, book->fullFileName);

   return match;
}

//Try matching the regexp in "rmp->regprog" with file name "name".
//Note that rmp->regprog may become NULL when switching regexp engine.
//Return "name" when there is a match, NULL when not.
private CS
fname_match(RegMatch* rmp, CS name){
   // extra check for valid arguments
   if (!name || !rmp->regprog)
      return NULL;

   // Ignore case when 'fileignorecase' or the argument is set.
   rmp->rm_ic = false;
   CS match = null;
   if (eeRegexec(rmp, name, (ColNr)0))
      match = name;
   ei (rmp->regprog) {
      // Replace $(HOME) with '~' and try matching again.
      CS p = home_replace_save(NULL, name);
      if (p && eeRegexec(rmp, p, (ColNr)0))
         match = name;
      eeglFree(p);
    }

    return match;
}

// Find a file in the book list by book number.
pub Book*
bookFindFileByBookNr(int nr){
   Byte key[SIZEOF_INT * 2 + 1];

   if (nr == 0)
      nr = curPor->altFnum;
   eeSnprintf(key, sizeof(key), "%x", nr);
   
   EeSetItem* hi = hash_find(&buf_hashtab, (Text){.c = key, .len = sizeof(key)});

   if (!HASHITEM_EMPTY(hi))
      return (Book *)(hi->hi_key - ((unsigned)(curBook->keyContainer - (CS)curBook)));
   return NULL;
}

//Get name of file 'n' in the book list. When the file has no name an empty string is returned.
//home_replace() is used to shorten the file name (used for marks).
//Return a pointer to allocated memory, of NULL when failed.
pub CS
bookGetNameByBookNr(int n, int fullname, Boole helptail) {   //for help books, return tail only
   Book* book = bookFindFileByBookNr(n);
   if (!book)
      return NULL;
   return home_replace_save(helptail ? book : NULL,
                 fullname ? book->fullFileName : book->currFileName);
}

//Set the "lnum" and "col" for book "book" and a portal into it.
//When "copy_options" is true save the local portal option values.
//When "lnum" is 0 only do the options.
pub void
bookSetPosInPort(
   Book* book,
   Portal* port,      // may be NULL when using :badd
   LineNr lnum,
   ColNr col,
   Boole copy_options
) {
   PortInfo* poInfo;

   FOR_ALL_BOOK_PORTINFOS(book, poInfo) {
      if (poInfo->portal == port)
         break;
   } 
   if (!poInfo) {
      // allocate a new entry
      poInfo = ALLOC_CLEAR_ONE(PortInfo);
      poInfo->portal = port;
      if (lnum == 0)      // set lnum even when it's 0
         lnum = 1;
   } else {
      // remove the entry from the list
      if (poInfo->prev)
         poInfo->prev->next = poInfo->next;
      else
         book->portInfos = poInfo->next;
      if (poInfo->next)
         poInfo->next->prev = poInfo->prev;
      if (copy_options && poInfo->isOptChanged) {
         optClearPortOptions(&poInfo->opt);
         deleteFoldRecurse(&poInfo->folds);
      }
   }
   if (lnum != 0) {
      poInfo->wi_fpos.lnum = lnum;
      poInfo->wi_fpos.col = col;
   }
   if (port)
      poInfo->wi_changelistidx = port->changeListInd;
   if (copy_options && port) {
      // Save the portal-specific option values.
      copyPortOpt(&poInfo->opt, &port->o);
      poInfo->foldManual = port->foldManual;
      cloneFoldArrayList(&port->folds, &poInfo->folds);
      poInfo->isOptChanged = true;
   }

   // insert the entry in front of the list
   poInfo->next = book->portInfos;
   book->portInfos = poInfo;
   poInfo->prev = NULL;
   if (poInfo->next)
      poInfo->next->prev = poInfo;
}

// Return true when "poInfo" has 'diff' set and the diff is only for another tab.  
// That's because a diff is local to a tab.
private int
wininfo_other_tab_diff(PortInfo* poInfo){
   if (!poInfo->opt.diff)
      return false;

   Portal* po;
   FOR_ALL_PORTALS(po) {
      // return false when it's a portal into current tab, thus the book was in diff mode here
      if (poInfo->portal == po)
         return false;
   } 
   return true;
}

//Find info for the current portal in book "book".
//If not found, return the info for the most recently used portal.
//When "need_options" is true skip entries where isOptChanged is false.
//When "skipDiffBook" is true avoid portals with 'diff' set that is in another tab.
//Return NULL when there isn't any info.
private PortInfo*
find_wininfo(Book* book, int need_options, Boole skipDiffBook){
   PortInfo* poInfo;
   FOR_ALL_BOOK_PORTINFOS(book, poInfo) {
      if (poInfo->portal == curPor
            && (!skipDiffBook || !wininfo_other_tab_diff(poInfo))
            && (!need_options || poInfo->isOptChanged)
      )
         break;
   } 

   if (poInfo)
      return poInfo;

   //If no wininfo for curPor, use the first in the list (that doesn't have
   //'diff' set and is in another tab).
   //If "need_options" is true skip entries that don't have options set,
   //unless the portal is editing "book", so we can copy from the portal itself.
   if (skipDiffBook) {
      FOR_ALL_BOOK_PORTINFOS(book, poInfo)
         if (!wininfo_other_tab_diff(poInfo)
             && (!need_options || poInfo->isOptChanged || (poInfo->portal && poInfo->portal->book == book))
         )
            break;
   } else
      poInfo = book->portInfos;
   return poInfo;
}

//Reset the local portal options to the values last used in this portal. If the book wasn't used 
//in this portal before, use the values from the most recently used portal. If the values were 
//never set, use the global values for the portal.
pub void
get_winopts(Book* book) {
   optClearPortOptions(&curPor->o);
   clearFolding(curPor);

   PortInfo* poInfo = find_wininfo(book, true, true);
   if (poInfo && poInfo->portal && poInfo->portal != curPor && poInfo->portal->book == book) {
      // The book is currently displayed in the portal: use the actual
      // option values instead of the saved (possibly outdated) values.
      Portal *po = poInfo->portal;

      copyPortOpt(&curPor->o, &po->o);
      curPor->foldManual = po->foldManual;
      curPor->foldNeedsRecomputation = true;
      cloneFoldArrayList(&po->folds, &curPor->folds);
   } ei (poInfo && poInfo->isOptChanged) {
      // the book was displayed in the current portal earlier
      copyPortOpt(&curPor->o, &poInfo->opt);
      curPor->foldManual = poInfo->foldManual;
      curPor->foldNeedsRecomputation = true;
      cloneFoldArrayList(&poInfo->folds, &curPor->folds);
   }
   if (poInfo)
      curPor->changeListInd = poInfo->wi_changelistidx;

   // Set 'foldlevel' to 'foldlevelstart' if it's not negative.
   if (foldLevelStart >= 0)
      curPor->o.foldLevel = foldLevelStart;
   afterCopyPortOpt(curPor);
}

//Find the position (lnum and col) for the book 'book' for the current portal.
//Return a pointer to no_position if no position is found.
pub Pos *
bookFindFpos(Book* book){
   static Pos no_position = {1, 0, 0};
   PortInfo* poInfo = find_wininfo(book, false, false);
   return poInfo ? &(poInfo->wi_fpos) : &no_position;
}

// List all known file names (for :files and :books command).
pub void
bookListFiles(Invocation* invo) {
   Book* book = firstBook;
   int i;
   int ro_char;
   int changed_char;
   int job_running;
   int job_none_open;

   ArrayList buflist;
   Book** booklistData = NULL;

   if (firstOccurrence(invo->arg, 't')) {
      ga_init2(&buflist, sizeof(Book *), 50);
      FOR_ALL_BOOKS(book) {
         if (ga_grow(&buflist, 1) == OK)
            ((Book **)buflist.c)[buflist.len++] = book;
      }

      qsort(buflist.c, (Unt)buflist.len, sizeof(Book *), bookCompare);

      booklistData = (Book **)buflist.c;
      book = *booklistData;
   }
   Book** p = booklistData;

   int len;
   for (; book && !gotInterruptG; book = booklistData
       ? (++p < booklistData + buflist.len ? *p : NULL)
       : book->next
   ){
      job_running = term_job_running(book->term);
      job_none_open = term_none_open(book->term);
      // skip unlisted books, unless ! was used
      if ((!book->o.bookListed && !invo->forceit && !firstOccurrence(invo->arg, 'u'))
            || (firstOccurrence(invo->arg, 'u') && book->o.bookListed)
            || (firstOccurrence(invo->arg, '+') 
                  && ((book->flags & BF_READERR) || !bookWasChanged(book))
               )
            || (firstOccurrence(invo->arg, 'a') 
                  && (book->mem.mfile == NULL || book->countPortals == 0)
               )
            || (firstOccurrence(invo->arg, 'h') 
                  && (book->mem.mfile == NULL || book->countPortals != 0)
               )
            || (firstOccurrence(invo->arg, 'R') && (!job_running || (job_running && job_none_open)))
            || (firstOccurrence(invo->arg, '?') 
                  && (!job_running || (job_running && !job_none_open))
               )
            || (firstOccurrence(invo->arg, 'F') && (job_running || book->term == NULL))
            || (firstOccurrence(invo->arg, '-') && book->o.modifiable)
            || (firstOccurrence(invo->arg, '=') && book->o.modifiable)
            || (firstOccurrence(invo->arg, 'x') && !(book->flags & BF_READERR))
            || (firstOccurrence(invo->arg, '%') && book != curBook)
            || (firstOccurrence(invo->arg, '#') 
                  && (book == curBook || curPor->altFnum != book->fiNum))
               )
         continue;
      CS name = bookSpName(book);
      if (name)
         copySubstrToAllocation(OUT nameBuffG, (Text){name, MAXPATHL - 1});
      ei (book && book->kind == BOOK_HELP) { 
         strPrintShortName(book->currFileName, nameBuffG, MAXPATHL);
      } else
         home_replace(book->currFileName, nameBuffG, MAXPATHL, true);
      if (message_filtered(nameBuffG))
         continue;

      changed_char = (book->flags & BF_READERR) ? 'x' : (bookWasChanged(book) ? '+' : ' ');
      if (job_running) {
         if (job_none_open)
            ro_char = '?';
         else
            ro_char = 'R';
         changed_char = ' ';  // bookWasChanged() returns true to avoid
                // closing, but it's not actually changed.
      } ei (book->term)
         ro_char = 'F';
      else
         ro_char = !book->o.modifiable ? '-' : '=';

      msg_putchar('\n');
      len = (int)eeSnprintfSafelen(IObuff, IOSIZE - 20, "%3d%c%c%c%c%c \"%s\"",
         book->fiNum,
         book->o.bookListed ? ' ' : 'u',
         book == curBook ? '%' : (curPor->altFnum == book->fiNum ? '#' : ' '),
         book->mem.mfile == NULL ? ' ' : (book->countPortals == 0 ? 'h' : 'a'),
         ro_char,
         changed_char,
         nameBuffG
      );

      // put "line 999" in column 40 or after the file name
      i = 40 - eeglStrSize(IObuff);
      do
         IObuff[len++] = ' ';
      while (--i > 0 && len < IOSIZE - 18);
      if (firstOccurrence(invo->arg, 't') && book->lastUsed)
         add_time(IObuff + len, (Unt)(IOSIZE - len), book->lastUsed);
      else
         eeSnprintf(IObuff + len, (Unt)(IOSIZE - len),
             _("line %ld"), book == curBook ? curPor->cursor.lnum : (long)findLnum(book));
      msg_outtrans(IObuff);
      out_flush();       // output one line at a time
      ui_breakcheck();
   }

   if (booklistData)
      ga_clear(&buflist);
}

//Get file name and line number for file 'fnum'. Used by DoOneCmd() for translating '%' and '#'.
//Used by insert_reg() and cmdline_paste() for '#' register. Return FAIL if not found, or OK.
pub int
bookGetFnameByFileId(int fnum, OUT CS* fname, OUT LineNr* lnum){
   Book* book = bookFindFileByBookNr(fnum);
   if (!book || book->currFileName == NULL)
      return FAIL;

   *fname = book->currFileName;
   *lnum = findLnum(book);

   return OK;
}

//Set the file name for "book"' to "ffname_arg", short file name to "sfname_arg".
//The file name with the full path is also remembered, for when :cd is used.
//Return FAIL for failure (file name already in use by other book) OK otherwise.
pub int
setfname(Book* book, CS ffname_arg, CS sfname_arg, Boole message) {   // give message when book already exists
   CS fullFName = ffname_arg;
   CS sfname = sfname_arg;
   Book* obook = NULL;
   FileStat   st;

   if (fullFName == NULL || *fullFName == ZERO) {
      // Removing the name.
      if (book->shortFileName != book->fullFileName)
         EE_CLEAR(book->shortFileName);
      else
         book->shortFileName = NULL;
      EE_CLEAR(book->fullFileName);
      st.st_dev = (Device)-1;
   } else {
      fname_expand(OUT &fullFName, &sfname); // will allocate fullFName
      if (!fullFName)          // out of memory
         return FAIL;

      //If the file name is already used in another book:
      //- if the book is loaded, fail
      //- if the book is not loaded, delete it from the list
      if (stat((char *)fullFName, &st) < 0)
         st.st_dev = (Device)-1;
      if (!(book->flags & BF_DUMMY))
         obook = booklistFindName_stat(fullFName, &st);
      if (obook && obook != book) {
         Portal   *port;
         Tab   *tab;
         Boole in_use = false;

         // during startup a portal may use a book that is not loaded yet
         FOR_ALL_TAB_PORTALS(tab, port) {
            if (port->book == obook)
               in_use = true;
         } 

         // it's loaded or used in a portal, fail
         if (obook->mem.mfile || in_use) {
            if (message)
               emsg(_(e_buffer_with_this_name_already_exists));
            eeglFree(fullFName);
            return FAIL;
         }
         // delete from the list
         bookClose(NULL, obook, DOBOOK_WIPE, false, false);
      }
      sfname = copyStr(sfname);
      if (!fullFName || !sfname) {
         eeglFree(sfname);
         eeglFree(fullFName);
         return FAIL;
      }
      if (book->shortFileName != book->fullFileName)
         eeglFree(book->shortFileName);
      eeglFree(book->fullFileName);
      book->fullFileName = fullFName;
      book->shortFileName = sfname;
   }
   book->currFileName = book->shortFileName;
   if (st.st_dev == (Device)-1)
      book->isDevNumValid = false;
   else {
      book->isDevNumValid = true;
      book->devNum = st.st_dev;
      book->inode = st.st_ino;
   }

   bookHandleNameChange(book);
   return OK;
}

// Crude way of changing the name of a book.  Use with care!
// The name should be relative to the current directory.
pub void
bookSetName(int fnum, CS name) {
   Book* book = bookFindFileByBookNr(fnum);
   if (!book)
      return;

   if (book->shortFileName != book->fullFileName)
      eeglFree(book->shortFileName);
   eeglFree(book->fullFileName);
   book->fullFileName = copyStr(name);
   book->shortFileName = NULL;
   // Allocate fullFName and expand into full path.
   fname_expand(&book->fullFileName, &book->shortFileName);
   book->currFileName = book->shortFileName;
}

// Take care of what needs to be done when the name of book has changed.
pub void
bookHandleNameChange(Book* book) {
   // If the file name changed, also change the name of the swapfile
   if (book->mem.mfile)
      ml_setname(book);

   if (book->term != NULL)
      term_clear_status_text(book->term);

   if (curPor->book == book)
      check_arg_idx(curPor);   // check file name for arg list
   status_redraw_all();   // status lines need to be redrawn
   fmarks_check_names(book);   // check named file marks
   ml_timestamp(book);      // reset timestamp
}

//set alternate file name for current portal
//
//Used by do_one_cmd(), do_write() and startEditingFile(). Return the book.
pub Book *
setaltfname(CS fullFName, CS sfname, LineNr lnum){
   // Create a book.  'buflisted' is not set if it's a new book
   Book* book = bookNew(fullFName, sfname, lnum, 0);
   if (book && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
      curPor->altFnum = book->fiNum;
   return book;
}

// Get alternate file name for current portal.
// Return NULL if there isn't any, and give error message if requested.
pub CS
getaltfname(int errmsg) {      // give error message
   CS fname;
   LineNr dummy;
   if (bookGetFnameByFileId(0, OUT &fname, OUT &dummy) == FAIL) {
      if (errmsg)
         emsg(_(e_no_alternate_file));
      return NULL;
   }
   return fname;
}

//Open a file and add it to the booklist. Return its number or 0 if failed.
//Use same flags as bookNew(), except BLN_DUMMY. Used by qfInit(), main() and doarglist()
pub int
bookOpen(CS fname, Unt flags){
   Book* book = bookNew(fname, NULL, (LineNr)0, flags);
   if (book)
      return book->fiNum;
   return 0;
}

// Return true if 'fullFName' is not the same file as current file.
// Fname must have a full path (expanded by mch_FullName()).
pub Boole
fNameMatchesCurBook(CS fullFName){
   return sameFileInBook(curBook, fullFName, NULL);
}

// Set inode and device number for a book. Must always be called when currFileName is changed!.
pub void
buf_setino(Book* book) {
   FileStat   st;
   if (book->currFileName && STAT(book->currFileName, &st) >= 0) {
      book->isDevNumValid = true;
      book->devNum = st.st_dev;
      book->inode = st.st_ino;
   } else
      book->isDevNumValid = false;
}

// Return true if dev/ino in book "book" matches with "stp".
private int
areSameInode(Book* book, FileStat* stp){
   return (book->isDevNumValid && stp->st_dev == book->devNum && stp->st_ino == book->inode);
}

// Print info about the current book.
pub void
fileinfo(
   Boole fullname,       // when true, print full path; whan > 1, include book number
   Boole shorthelp,
   Boole dont_truncate
){
   Unt bufLen = 0;
   CS buf = alloc(IOSIZE);

   if (fullname > 1)       // 2 CTRL-G: include book number
      bufLen = eeSnprintfSafelen(buf, IOSIZE, "buf %d: ", curBook->fiNum);

   buf[bufLen++] = '"';

   CS name = bookSpName(curBook);
   if (name)
      bufLen += eeSnprintfSafelen(buf + bufLen, IOSIZE - bufLen, "%s", name);
   else {
      if (!fullname && curBook->currFileName)
         name = curBook->currFileName;
      else
         name = curBook->fullFileName;
      if (shorthelp && curBook->kind == BOOK_HELP) {
         strPrintShortName(name, (CS)buf + bufLen, IOSIZE - (int)bufLen);
      } else {
         home_replace(name, (CS)buf + bufLen, IOSIZE - (int)bufLen, true);
      }
      bufLen += STRLEN(buf + bufLen);
   }

   bufLen += eeSnprintfSafelen(
      buf + bufLen,
      IOSIZE - bufLen,
      "\"%s%s%s%s%s%s",
      bookWasChanged(curBook) ? (S" (+)") : S" ",
      (curBook->flags & BF_NOTEDITED) && !bookDontWrite(curBook) ? _("[Not edited]") : S"",
      (curBook->flags & BF_NEW) && !bookDontWrite(curBook) ? new_file_message() : S"",
      (curBook->flags & BF_READERR) ? _("[Read errors]") : S"", 
      curBook->o.modifiable ? S"" : S"[-]",
      (bookWasChanged(curBook) || (curBook->flags & BF_WRITE_MASK) || !curBook->o.modifiable) ? S" " : S"" 
   );

   if (curBook->mem.flags & ML_EMPTY) {
      bufLen += eeSnprintfSafelen(
         buf + bufLen, IOSIZE - bufLen, "%s", _(no_lines_msg)
      );
   } else {
      // Current line and column are already on the screen -- webb
      bufLen += eeSnprintfSafelen(
          buf + bufLen,
          IOSIZE - bufLen,
          NGETTEXT("%ld line --%d%%--", "%ld lines --%d%%--", curBook->mem.lineCount),
          (long)curBook->mem.lineCount,
          calc_percentage(curPor->cursor.lnum, curBook->mem.lineCount)
      );
   } 

   (void)append_arg_number(curPor, buf + bufLen, IOSIZE - bufLen);

   if (dont_truncate) {
      // Temporarily set msg_scroll to avoid the message being truncated.
      // First call msg_start() to get the message in the right place.
      msg_start();
      int n = msg_scroll;
      msg_scroll = true;
      msg(buf);
      msg_scroll = n;
   } else {
      CS p = msgTruncDeco(buf, 0);
      if (restart_edit != 0 || (msg_scrolled && !need_wait_return))
          // Need to repeat the message after redrawing when:
          // - When restart_edit is set (otherwise there will be a delay before redrawing).
          // - When the screen was scrolled but there is no wait-return prompt.
          set_keep_msg((CS)p, 0);
   }

   eeglFree(buf);
}

pub int
col_print(CS buf, Unt  buflen, int col, int vcol){
   if (col == vcol)
      return (int)eeSnprintfSafelen(buf, buflen, "%d", col);
   return (int)eeSnprintfSafelen(buf, buflen, "%d-%d", col, vcol);
}

// Used for building in the status line.
typedef struct {
   CS start;
   int minWidth;
   int maxWidth;
   enum {
      Normal,
      Empty,
      Group,
      Separate,
      Highlight,
      TabPage,
      Trunc
   } StatusTag;
} StatusItem;

private Unt countStatusItems = 20; // Initial value, grows as needed.
private Arr(StatusItem) statusItemsP = NULL;
private int* stlGroupItemP = NULL;
private StatusLineHilite* stl_tabtab = NULL;
private int* stlSeparatorLocationsP = NULL;

//Build a string from the status line items in "fmt". Return length of string in screen cells.
//
//Items are drawn interspersed with the text that surrounds it
//Specials: %-<wid>(xxx%) => group, %= => separation marker, %< => truncation
//Item: %-<minwid>.<maxwid><itemch> All but <itemch> are optional
//
//If maxwidth is not zero, the string will be filled at any middle marker
//or truncated if too long, fillchar is used for all whitespace.
pub int
bookRenderStatusLine(
   Portal* po,
   OUT CS out,      // string book to write into != nameBuffG
   Unt outlen,      // length of out[]
   CS fmt,
   Byte oname,      // one of STATLINE_* constants
   int opt_scope,   // scope for "oname"
   Unt fillchar,
   int maxwidth,
   OUT Arr(StatusLineHilite)* labels   // return: tab numbers (can be NULL)
){
   CS p;
   CS s;
   int save_VIsual_active;
   long l;
   int prevchar_isflag;
   int prevchar_isitem;
   int itemisflag;
   int fillable;
   CS str;
   long num;
   int minwid;
   int maxwid;
   int zeropad;
   Byte base;
   Byte opt;
#define TMPLEN 70
   Byte buf_tmp[TMPLEN];
   CS usefmt = fmt;
   StatusLineHilite *sp;
   int save_redraw_not_allowed = redraw_not_allowed;
   int save_keyWasTypedG = keyWasTypedG;
   // TODO: find out why using called_emsg_before makes tests fail, does it matter?
   // int   called_emsg_before = called_emsg;
   int anyEmsgSaved = anyEmsgG;

   // When inside drawUpdateScreen() we do not want redrawing a statusline,
   // ruler, title, etc. to trigger another redraw, it may cause an endless loop.
   if (updating_screen)
      redraw_not_allowed = true;

   if (!statusItemsP) {
      statusItemsP = ALLOC_MULT(StatusItem, countStatusItems);
      stlGroupItemP = ALLOC_MULT(int, countStatusItems);

      // Allocate one more, because the last element is used to indicate the end of the list
      stl_tabtab = ALLOC_MULT(StatusLineHilite, countStatusItems + 1);

      stlSeparatorLocationsP = ALLOC_MULT(int, countStatusItems);
   }

   //When the format starts with "%!" then evaluate it as an expression and
   //use the result as the actual format string.
   if (fmt[0] == '%' && fmt[1] == '!') {
      Var tv;
      tv.tag = VAR_NUMBER;
      tv.number = po->id;
      set_var(tConst("g:statusline_winid"), &tv, false);

      usefmt = eval_to_string_safe(fmt + 2, false);
      if (usefmt == NULL)
         usefmt = fmt;

      unletImpl(S"g:statusline_winid", true);
   }

   if (fillchar == ZERO)
      fillchar = ' ';

   // The cursor in portals other than the current one isn't always
   // up-to-date, esp. because of autocommands and timers.
   LineNr lnum = po->cursor.lnum;
   if (lnum > po->book->mem.lineCount) {
      lnum = po->book->mem.lineCount;
      po->cursor.lnum = lnum;
   }

   //Get line & check if empty (cursorpos will show "0-1").  Note that
   //p will become invalid when getting another book line.
   p = memGetLine(po->book, lnum, false);
   Boole empty_line = (*p == ZERO);

   //Get the byte value now, in case we need it below. This is more efficient
   //than making a copy of the line.
   ColNr len = memGetBookLen(po->book, lnum);
   
   Unt byteval;
   if (po->cursor.col > len) {
      // Line may have changed since checking the cursor column, or the lnum was adjusted above
      po->cursor.col = len;
      po->cursor.coladd = 0;
      byteval = 0;
   } else
      byteval = mb_ptr2char(p + po->cursor.col);

   int groupdepth = 0;
   int evaldepth = 0;
   p = out;
   int curitem = 0;
   prevchar_isflag = true;
   prevchar_isitem = false;
   for (s = usefmt; *s != ZERO; ) {
      if (curitem == (int)countStatusItems) {
         Unt   newLen = countStatusItems * 3 / 2;

         StatusItem* new_items = eeRealloc(statusItemsP, sizeof(StatusItem) * newLen);
         statusItemsP = new_items;

         int *new_groupitem = eeRealloc(stlGroupItemP, sizeof(int) * newLen);
         stlGroupItemP = new_groupitem;

         Arr(StatusLineHilite) new_hlrec = 
            eeRealloc(stl_tabtab, sizeof(StatusLineHilite) * (newLen + 1));
         stl_tabtab = new_hlrec;

         int* new_separator_locs = eeRealloc(stlSeparatorLocationsP, sizeof(int) * newLen);
         stlSeparatorLocationsP = new_separator_locs;
         countStatusItems = newLen;
      }

      if (*s != '%')
         prevchar_isflag = prevchar_isitem = false;

      // Handle up to the next '%' or the end.
      while (*s != ZERO && *s != '%' && p + 1 < out + outlen)
         *p++ = *s++;
      if (*s == ZERO || p + 1 >= out + outlen)
         break;

      // Handle one '%' item.
      s++;
      if (*s == ZERO)  // ignore trailing %
         break;
      if (*s == '%') {
         if (p + 1 >= out + outlen)
            break;
         *p++ = *s++;
         prevchar_isflag = prevchar_isitem = false;
         continue;
      }
      // STL_SEPARATE: Separation between items, filled with white space.
      if (*s == STL_SEPARATE) {
         s++;
         if (groupdepth > 0)
            continue;
         statusItemsP[curitem].StatusTag = Separate;
         statusItemsP[curitem++].start = p;
         continue;
      }
      if (*s == STL_TRUNCMARK) {
         s++;
         statusItemsP[curitem].StatusTag = Trunc;
         statusItemsP[curitem++].start = p;
         continue;
      }
      if (*s == ')') {
         s++;
         if (groupdepth < 1)
            continue;
         groupdepth--;

         CS t = statusItemsP[stlGroupItemP[groupdepth]].start;
         *p = ZERO;
         l = eeglStrSize(t);
         if (curitem > stlGroupItemP[groupdepth] + 1 
               && statusItemsP[stlGroupItemP[groupdepth]].minWidth == 0
         ) {
            Short groupStartUserId = 0;
            Short groupEndHiId = 0;

            // remove group if all items are empty and hilite group doesn't change
            Long n;
            for (n = stlGroupItemP[groupdepth] - 1; n >= 0; n--) {
               if (statusItemsP[n].StatusTag == Highlight) {
                  groupStartUserId = groupEndHiId = statusItemsP[n].minWidth;
                  break;
               }
            }
            for (n = stlGroupItemP[groupdepth] + 1; n < curitem; n++) {
               if (statusItemsP[n].StatusTag == Normal)
                  break;
               if (statusItemsP[n].StatusTag == Highlight)
                  groupEndHiId = statusItemsP[n].minWidth;
            }
            if (n == curitem && groupStartUserId == groupEndHiId) {
               // empty group
               p = t;
               l = 0;
               for (n = stlGroupItemP[groupdepth] + 1; n < curitem; n++) {
                  // do not use the hiliting from the removed group
                  if (statusItemsP[n].StatusTag == Highlight)
                      statusItemsP[n].StatusTag = Empty;
                  // adjust the start position of TabPage to the next item position
                  if (statusItemsP[n].StatusTag == TabPage)
                      statusItemsP[n].start = p;
               }
            }
         }
         if (l > statusItemsP[stlGroupItemP[groupdepth]].maxWidth) {
            //truncate, remove n bytes of text at the start
            //Find the first character that should be included.
            Long n = 0;
            while (l >= statusItemsP[stlGroupItemP[groupdepth]].maxWidth) {
               l -= bookPtr2Cells(t + n);
               n += utfCharLen(t + n);
            }

            *t = '<';
            MEMMOVE(t + 1, t + n, (Unt)(p - (t + n)));
            p = p - n + 1;

            // Fill up space left over by half a double-wide char.
            while (++l < statusItemsP[stlGroupItemP[groupdepth]].minWidth)
               MB_CHAR2BYTES(fillchar, p);

            // correct the start of the items for the truncation
            for (l = stlGroupItemP[groupdepth] + 1; l < curitem; l++) {
               // Minus one for the leading '<' added above.
               statusItemsP[l].start -= n - 1;
               if (statusItemsP[l].start < t)
                  statusItemsP[l].start = t;
            }
         } ei (abs(statusItemsP[stlGroupItemP[groupdepth]].minWidth) > l) {
            // fill
            Long n = statusItemsP[stlGroupItemP[groupdepth]].minWidth;
            if (n < 0) {
               // fill by appending characters
               n = 0 - n;
               while (l++ < n && p + 1 < out + outlen)
                  MB_CHAR2BYTES(fillchar, p);
            } else {
               // fill by inserting characters
               l = (n - l) * MB_CHAR2LEN(fillchar);
               MEMMOVE(t + l, t, (Unt)(p - t));
               if (p + l >= out + outlen)
                  l = (long)((out + outlen) - p - 1);
               p += l;
               for (n = stlGroupItemP[groupdepth] + 1; n < curitem; n++)
                  statusItemsP[n].start += l;
               for ( ; l > 0; l--)
                  MB_CHAR2BYTES(fillchar, t);
            }
         }
         continue;
      }
      minwid = 0;
      maxwid = 9999;
      zeropad = false;
      l = 1;
      if (*s == '0') {
         s++;
         zeropad = true;
      }
      if (*s == '-') {
         s++;
         l = -1;
      }
      if (EE_ISDIGIT(*s)) {
         minwid = (int)parseLong(&s);
         if (minwid < 0)   // overflow
            minwid = 0;
      }
      if (*s == STL_USER_HL) {
         statusItemsP[curitem].StatusTag = Highlight;
         statusItemsP[curitem].start = p;
         statusItemsP[curitem].minWidth = minwid > 9 ? 1 : minwid;
         s++;
         curitem++;
         continue;
      }
      if (*s == STL_TABPAGENR || *s == STL_TABCLOSENR) {
         if (*s == STL_TABCLOSENR) {
            if (minwid == 0) {
               // %X ends the close label, go back to the previously define tab label nr.
               for (Long n = curitem - 1; n >= 0; --n) {
                  if (statusItemsP[n].StatusTag == TabPage && statusItemsP[n].minWidth >= 0) {
                      minwid = statusItemsP[n].minWidth;
                      break;
                  }
               } 
            } else
               // close nrs are stored as negative values
               minwid = - minwid;
         }
         statusItemsP[curitem].StatusTag = TabPage;
         statusItemsP[curitem].start = p;
         statusItemsP[curitem].minWidth = minwid;
         s++;
         curitem++;
         continue;
      }
      if (*s == '.') {
         s++;
         if (EE_ISDIGIT(*s)) {
            maxwid = (int)parseLong(&s);
         if (maxwid <= 0)   // overflow
             maxwid = 50;
         }
      }
      minwid = (minwid > 50 ? 50 : minwid) * l;
      if (*s == '(') {
         stlGroupItemP[groupdepth++] = curitem;
         statusItemsP[curitem].StatusTag = Group;
         statusItemsP[curitem].start = p;
         statusItemsP[curitem].minWidth = minwid;
         statusItemsP[curitem].maxWidth = maxwid;
         s++;
         curitem++;
         continue;
      }
      // Denotes end of expanded %{} block
      if (*s == '}' && evaldepth > 0) {
          s++;
          evaldepth--;
          continue;
      }
      if (firstOccurrence(STL_ALL, *s) == NULL) {
         if (*s == ZERO)  // can happen with "%0"
            break;
         s++;
         continue;
      }
      opt = *s++;

      // OK - now for the real work
      base = 'D';
      itemisflag = false;
      fillable = true;
      num = -1;
      str = NULL;
      switch (opt) {
      case STL_FILEPATH:
      case STL_FULLPATH:
      case STL_FILENAME: {
         fillable = false;   // don't change ' ' to fillchar
         CS name = bookSpName(po->book);
         if (name)
            copySubstrToAllocation(OUT nameBuffG, (Text){name, MAXPATHL - 1});
         else {
            CS t = (opt == STL_FULLPATH) ? po->book->fullFileName : po->book->currFileName;
            if (po->book->kind == BOOK_HELP) {
               strPrintShortName(t, nameBuffG, MAXPATHL);
            } else {
               home_replace(t, nameBuffG, MAXPATHL, true);
            }
         }
         trans_characters(nameBuffG, MAXPATHL);
         if (opt != STL_FILENAME)
            str = nameBuffG;
         else
            str = fiGetShortFiName(nameBuffG);
         break;
      }

      case STL_EE_EXPR: { // opening curly brace
         CS block_start = s - 1;
         Boole reevaluate = (*s == '%');

         if (reevaluate)
            s++;
         itemisflag = true;
         CS t = p;
         while ((*s != '}' || (reevaluate && s[-1] != '%')) && *s != ZERO && p + 1 < out + outlen)
            *p++ = *s++;
         if (*s != '}')   // missing '}' or out of space
            break;
         s++;
         if (reevaluate)
            p[-1] = ZERO; // remove the % at the end of %{% expr %}
         else
            *p = ZERO;
         p = t;
         eeSnprintf(buf_tmp, sizeof(buf_tmp), "%d", curBook->fiNum);
         set_internal_string_var(S"g:actual_curbuf", buf_tmp);
         eeSnprintf(buf_tmp, sizeof(buf_tmp), "%d", curPor->id);
         set_internal_string_var(S"g:actual_curPor", buf_tmp);

         Book* curBookSaved = curBook;
         Portal* save_curPor = curPor;
         save_VIsual_active = VIsual_active;
         curPor = po;
         curBook = po->book;
         // Visual mode is only valid in the current portal.
         if (curPor != save_curPor)
            VIsual_active = false;

         str = eval_to_string_safe(p, false);

         curPor = save_curPor;
         curBook = curBookSaved;
         VIsual_active = save_VIsual_active;
         unletImpl(S"g:actual_curbuf", true);
         unletImpl(S"g:actual_curPor", true);

         if (str && *str != ZERO) {
            if (*skipdigits(str) == ZERO) {
                num = atoi((char *)str);
                EE_CLEAR(str);
                itemisflag = false;
            }
         }

          //If the output of the expression needs to be evaluated
          //replace the %{} block with the result of evaluation
          if (reevaluate && str && *str != ZERO
             && strchr((const char *)str, '%') != NULL
             && evaldepth < MAX_STL_EVAL_DEPTH
         ) {
            Unt parsed_usefmt = (Unt)(block_start - usefmt);
            Unt str_length = strlen((const char *)str);
            Unt fmt_length = strlen((const char *)s);
            Unt new_fmt_len = parsed_usefmt + str_length + fmt_length + 3;
            CS new_fmt = (CS)alloc(new_fmt_len * sizeof(Byte));

            CS new_fmt_p = new_fmt;

            new_fmt_p = (CS)memcpy(new_fmt_p, usefmt, parsed_usefmt) + parsed_usefmt;
            new_fmt_p = (CS)memcpy(new_fmt_p , str, str_length) + str_length;
            new_fmt_p = (CS)memcpy(new_fmt_p, "%}", 2) + 2;
            new_fmt_p = (CS)memcpy(new_fmt_p , s, fmt_length) + fmt_length;
            *new_fmt_p = 0;
            new_fmt_p = NULL;

            if (usefmt != fmt)
               eeglFree(usefmt);
            EE_CLEAR(str);
            usefmt = new_fmt;
            s = usefmt + parsed_usefmt;
            evaldepth++;
            continue;
         }
         break;
      }
      case STL_LINE:
          num = (po->book->mem.flags & ML_EMPTY) ? 0L : (long)(po->cursor.lnum);
          break;

      case STL_NUMLINES:
          num = po->book->mem.lineCount;
          break;

      case STL_COLUMN:
          num = (stateG & MODE_INSERT) == 0 && empty_line ? 0 : (int)po->cursor.col + 1;
          break;

      case STL_VIRTCOL:
      case STL_VIRTCOL_ALT: {
         ColNr virtcol = po->virtCol + 1;

         // Don't display %V if it's the same as %c.
         if (opt == STL_VIRTCOL_ALT
             && (virtcol == (ColNr)((stateG & MODE_INSERT) == 0
                   && empty_line ? 0 : (int)po->cursor.col + 1))
         )
            break;
         num = (long)virtcol;
         break;
      }

      case STL_PERCENTAGE:
         num = calc_percentage((long)po->cursor.lnum, (long)po->book->mem.lineCount);
         break;

      case STL_ALTPERCENT:
         str = buf_tmp;
         (void)get_rel_pos(po, str, TMPLEN);
         break;

      case STL_SHOWCMD:
         if (p_sloc == oname)
            str = showcmd_buf;
         break;

      case STL_ARGLISTSTAT:
         fillable = false;
         buf_tmp[0] = ZERO;
         if (append_arg_number(po, buf_tmp, sizeof(buf_tmp)) > 0)
            str = buf_tmp;
         break;

      case STL_KEYMAP:
         fillable = false;
         if (drawGetKeymapStr(po, OUT (Text){buf_tmp, TMPLEN}) > 0)
            str = buf_tmp;
         break;
      case STL_PAGENUM:
         num = 0;
         break;

      case STL_BUFNO:
         num = po->book->fiNum;
         break;

      case STL_OFFSET_X:
         base = 'X';
         // FALLTHROUGH
      case STL_OFFSET:
         l = ml_find_line_or_offset(po->book, po->cursor.lnum, NULL);
         num = (po->book->mem.flags & ML_EMPTY) || l < 0
                ? 0L : l + 1 + ((stateG & MODE_INSERT) == 0 && empty_line
               ? 0 : (int)po->cursor.col);
         break;

      case STL_BYTEVAL_X:
         base = 'X';
         // FALLTHROUGH
      case STL_BYTEVAL:
         num = byteval;
         if (num == NL)
            num = 0;
         break;

      case STL_HELPFLAG:
      case STL_HELPFLAG_ALT:
         itemisflag = true;
         if (po->book->kind == BOOK_HELP)
            str = (CS)((opt == STL_HELPFLAG_ALT) ? S",HLP" : _("[Help]"));
         break;

      case STL_FILETYPE:
         if (po->book->fileType && STRLEN(po->book->fileType) < TMPLEN - 3) {
            eeSnprintf(buf_tmp, sizeof(buf_tmp), "[%s]", po->book->fileType);
            str = buf_tmp;
         }
         break;

      case STL_FILETYPE_ALT:
         itemisflag = true;
         if (po->book->fileType && STRLEN(po->book->fileType) < TMPLEN - 2) {
            eeSnprintf(buf_tmp, sizeof(buf_tmp), ",%s", po->book->fileType);
            for (CS t = buf_tmp; *t != 0; t++)
               *t = TOUPPER_LOC(*t);
            str = buf_tmp;
         }
         break;

      case STL_PREVIEWFLAG:
      case STL_PREVIEWFLAG_ALT:
         itemisflag = true;
         if (po->isPreview)
            str = (CS)((opt == STL_PREVIEWFLAG_ALT) ? S",PRV" : _("[Preview]"));
         break;

      case STL_QUICKFIX:
         if (isLocationListBook(po->book)) {
            str = (CS)(po->locationStackRef ? _(msg_loclist) : _(msg_qflist));
         } 
         break;

      case STL_MODIFIED:
      case STL_MODIFIED_ALT:
         itemisflag = true;
         switch (
               (opt == STL_MODIFIED_ALT) + bookWasChanged(po->book) * 2 
                + (!po->book->o.modifiable) * 4
         ) {
         case 2: str = S"(+)"; break;
         case 3: str = S",+"; break;
         case 4: 
         case 5: 
         case 6: 
         case 7: 
            str = S"[-]"; break;
         }
         break;

      case STL_HIGHLIGHT: {
         CS t = s;

         while (*s != '#' && *s != ZERO)
            ++s;
         if (*s == '#') {
            statusItemsP[curitem].StatusTag = Highlight;
            statusItemsP[curitem].start = p;
            statusItemsP[curitem].minWidth = -syntaxClusterByName((Text){.c = t, .len = s - t});
            curitem++;
         }
         if (*s != ZERO)
            ++s;
         continue;
      }
      }

      statusItemsP[curitem].start = p;
      statusItemsP[curitem].StatusTag = Normal;
      if (str && *str) {
         CS t = str;

         if (itemisflag) {
            if ((t[0] && t[1])
                  && ((!prevchar_isitem && *t == ',') || (prevchar_isflag && *t == ' '))
            )
               t++;
            prevchar_isflag = true;
         }
         l = eeglStrSize(t);
         if (l > 0)
            prevchar_isitem = true;
         if (l > maxwid) {
            while (l >= maxwid)
               l -= bookPtr2Cells(t);
            t += utfCharLen(t);
            if (p + 1 >= out + outlen)
                break;
            *p++ = '<';
         }
         if (minwid > 0) {
            for (; l < minwid && p + 1 < out + outlen; l++) {
               // Don't put a "-" in front of a digit.
               if (l + 1 == minwid && fillchar == '-' && EE_ISDIGIT(*t))
                  *p++ = ' ';
               else
                  MB_CHAR2BYTES(fillchar, p);
            }
            minwid = 0;
         } else
            minwid *= -1;
         for (; *t && p + 1 < out + outlen; t++) {
            // Change a space by fillchar, unless fillchar is '-' and a digit follows.
            if (fillable && *t == ' ' && (!EE_ISDIGIT(*(t + 1)) || fillchar != '-'))
               MB_CHAR2BYTES(fillchar, p);
            else
               *p++ = *t;
         }
         for (; l < minwid && p + 1 < out + outlen; l++)
            MB_CHAR2BYTES(fillchar, p);
      } ei (num >= 0) {
         int nbase = (base == 'D' ? 10 : (base == 'O' ? 8 : 16));
         Byte nstr[20];
         CS t = nstr;

         if (p + 20 >= out + outlen)
            break;      // not sufficient space
         prevchar_isitem = true;
         if (opt == STL_VIRTCOL_ALT) {
            *t++ = '-';
            minwid--;
         }
         *t++ = '%';
         if (zeropad)
            *t++ = '0';
         *t++ = '*';
         *t++ = nbase == 16 ? base : (Byte)(nbase == 8 ? 'o' : 'd');
         *t = ZERO;
         Long n;
         for (n = num, l = 1; n >= nbase; n /= nbase)
            l++;
         if (opt == STL_VIRTCOL_ALT)
            l++;

         if (l > maxwid) {
            l += 2;
            n = l - maxwid;
            while (l-- > maxwid)
               num /= nbase;
            *t++ = '>';
            *t++ = '%';
            *t = t[-3];
            *++t = ZERO;
            p += eeSnprintfSafelen(p, outlen - (p - out), (char *)nstr, 0, num, n);
         }
         else
            p += eeSnprintfSafelen(p, outlen - (p - out), (char *)nstr, minwid, num);
      } else
         statusItemsP[curitem].StatusTag = Empty;

      if (num >= 0 || (!itemisflag && str && *str != ZERO))
         prevchar_isflag = false;       // Item not NULL, but not a flag
      if (opt == STL_EE_EXPR)
         eeglFree(str);
      curitem++;
   }
   *p = ZERO;
   Unt outputlen = (Unt)(p - out);  // length of out[] used (excluding the ZERO)
   int itemcnt = curitem;

   if (usefmt != fmt)
      eeglFree(usefmt);

   int width = eeglStrSize(out);
   if (maxwidth > 0 && width > maxwidth) {
      //Result is too long, must truncate somewhere.
      l = 0;
      if (itemcnt == 0)
         s = out;
      else {
         for ( ; l < itemcnt; l++) {
            if (statusItemsP[l].StatusTag == Trunc) {
               //Truncate at %< item.
               s = statusItemsP[l].start;
               break;
            }
         } 
         if (l == itemcnt) {
            //No %< item, truncate first item.
            s = statusItemsP[0].start;
            l = 0;
         }
      }

      if (width - eeglStrSize(s) >= maxwidth) {
         //Truncation mark is beyond max length
         s = out;
         width = 0;
         for (;;) {
            width += bookPtr2Cells(s);
            if (width >= maxwidth)
               break;
            s += utfCharLen(s);
         }
         // Fill up for half a double-wide character.
         while (++width < maxwidth)
            MB_CHAR2BYTES(fillchar, s);
         for (l = 0; l < itemcnt; l++) {
            if (statusItemsP[l].start > s) {
               break;
            } 
         } 
         itemcnt = l;
         *s++ = '>';
         *s = ZERO;
      } else {
         CS end = out + outputlen;

         Long n = 0;
         while (width >= maxwidth) {
            width -= bookPtr2Cells(s + n);
            n += utfCharLen(s + n);
         }
         p = s + n;
         MEMMOVE(s + 1, p, (Unt)(end - p) + 1);   // +1 for ZERO
         end -= (Unt)(p - (s + 1));
         *s = '<';

         --n;   // count the '<'
         for (; l < itemcnt; l++) {
            if (statusItemsP[l].start - n >= s)
               statusItemsP[l].start -= n;
            else
               statusItemsP[l].start = s;
         }

         // Fill up for half a double-wide character.
         while (++width < maxwidth) {
            s = end;
            MB_CHAR2BYTES(fillchar, s);
            *s = ZERO;
            end = s;
         }
      }
      width = maxwidth;
   } ei (width < maxwidth && outputlen + maxwidth - width + 1 < outlen) {
      //Find how many separators there are, which we will use when
      //figuring out how many groups there are.
      int num_separators = 0;

      for (l = 0; l < itemcnt; l++) {
         if (statusItemsP[l].StatusTag == Separate) {
            // Create an array of the start location for each separator mark.
            stlSeparatorLocationsP[num_separators] = l;
            num_separators++;
         }
      }

      // If we have separated groups, then we deal with it now
      if (num_separators) {
         int standard_spaces = (maxwidth - width) / num_separators;
         int final_spaces = (maxwidth - width) - standard_spaces * (num_separators - 1);
         for (l = 0; l < num_separators; l++) {
            int dislocation = (l == (num_separators - 1)) ? final_spaces : standard_spaces;
            dislocation *= MB_CHAR2LEN(fillchar);
            CS start = statusItemsP[stlSeparatorLocationsP[l]].start;
            CS seploc = start + dislocation;
            STRMOVE(seploc, start);
            for (s = start; s < seploc;)
               MB_CHAR2BYTES(fillchar, s);

            for (int i = stlSeparatorLocationsP[l] + 1; i < itemcnt; i++)
               statusItemsP[i].start += dislocation;
          }

          width = maxwidth;
      }
   }

   // Store the info about tab labels.
   if (labels) {
      *labels = stl_tabtab;
      sp = stl_tabtab;
      for (l = 0; l < itemcnt; l++) {
         if (statusItemsP[l].StatusTag == TabPage) {
            sp->start = statusItemsP[l].start;
            sp->hiId = statusItemsP[l].minWidth;
            sp++;
         }
      }
      sp->start = NULL;
      sp->hiId = 0;
   }

   // A user function may reset vars, restore them
   redraw_not_allowed = save_redraw_not_allowed;
   keyWasTypedG = save_keyWasTypedG;

   //Check for an error.  If there is one the display will be messed up and
   //might loop redrawing.  Avoid that by making the corresponding option empty.
   //TODO: find out why using called_emsg_before makes tests fail, does it matter?
   //if (called_emsg > called_emsg_before)
   if (anyEmsgG > anyEmsgSaved) {
      CS optionName = oname == STATLINE_TABPANEL 
         ? S"tabpanel"
         : (oname == STATLINE_STATUSLINE)
            ? S"statusline"
            : S"rulerformat";
      optChangeStringOptionDirect(optionName, S"", opt_scope, SID_ERROR);
   } 

   return width;
}

// Get relative cursor position in portal into "buf[]", in the localized
// percentage form like %99, 99%; using "Top", "Bot" or "All" when appropriate.
pub int
get_rel_pos(Portal* po, CS buf, int buflen){
   long above; // number of lines above portal
   long below; // number of lines below portal

   if (buflen < 3) // need at least 3 chars for writing
      return 0;
   above = po->topLine - 1;
   above += diff_check_fill(po, po->topLine) - po->topFill;
   if (po->topLine == 1 && po->topFill >= 1)
      above = 0; // All book lines are displayed and there is an
                 // indication of filler lines, that can be considered seeing all lines.
   below = po->book->mem.lineCount - po->bottomLine + 1;
   if (below <= 0)
      return (int)eeSnprintfSafelen(buf, buflen,
          "%s", (above == 0) ? _("All") : _("Bot"));

   if (above <= 0)
      return (int)eeSnprintfSafelen(buf, buflen,
          "%s", _("Top"));

   int perc = calc_percentage(above, above + below);
   Byte tmp[8];
   // localized percentage value
   eeSnprintf(tmp, sizeof(tmp), _("%d%%"), perc);
   return (int)eeSnprintfSafelen(buf, buflen, _("%2s"), tmp);
}

// Append (file 2 of 8) to "buf[]", if editing more than one file. Return the number of appended 
// characters
private int
append_arg_number(Portal* po, CS buf, Unt buflen){
   if (ARGCOUNT <= 1)      // nothing to do
      return 0;
   
   CS msg = po->isNotValid ? _(" (%d of %d)") : _(" ((%d) of %d)");
   return (int)eeSnprintfSafelen(buf, buflen, msg, po->argListInd + 1, ARGCOUNT);
}

// Make "*fullFName" a full file name, set "*sfname" to "*fullFName" if not NULL.
// "*fullFName" becomes a pointer to allocated memory (or NULL).
// When resolving a link, both "*sfname" and "*fullFName" will point to the same
// allocated memory.
// The "*fullFName" and "*sfname" pointer values on call will not be freed.
// Note that the resulting "*fullFName" pointer should be considered not allocated.
pub void
fname_expand(CS* fullFName, CS* sfname){
   if (*fullFName == NULL)       // no file name given, nothing to do
      return;
   if (*sfname == NULL)       // no short file name given, use fullFName
      *sfname = *fullFName;
   *fullFName = fiExpandAndCopy(*fullFName, true);   // expand to full path
}

// Open a portal for a number of books.
pub void
c_bookAll(Invocation* invo) {
   Book* book;
   Portal *po, *wpnext;
   int split_ret = OK;
   int p_ea_save;
   int open_wins = 0;
   int count;      // Maximum number of portals to open.
   int all;      // When true also load inactive books.
   int had_tab = commModifierG.cmod_tab;
   Tab* tNext;

   if (invo->addr_count == 0)   // make as many portals as possible
      count = 9999;
   else
      count = invo->line2;   // make as many portals as specified
   if (invo->id == C_unhide || invo->id == C_sunhide)
      all = false;
   else
      all = true;

   //Stop Visual mode, the cursor and "VIsual" may very well be invalid after
   //switching to another book.
   reset_VIsual_and_resel();

   setpcmark();

   // Close superfluous portals (two portals into the same book).
   // Also close portals that are not full-width.
   if (had_tab > 0)
      gotoTab(firstTabG, true, true);
   for (;;) {
      tNext = curtab->next;
      for (po = firstPor; po; po = wpnext) {
         wpnext = po->next;
         if ((po->book->countPortals > 1
            || ((commModifierG.cmod_split & WSP_VERT)
                ? (po->height + STATUS_HEIGHT < visibleRowsG - commlineHeightG )
                : po->width != visibleColsG)
            || (had_tab > 0 && po != firstPor))
             && !ONLY_ONE_PORTAL
             && !(portalLocked(po) || po->book->locked > 0)
             && !portUnlisted(po)
         ){
            if (closePortal(po, false) == FAIL)
               break;
            // Just in case an autocommand does something strange with portals: start all over...
            wpnext = firstPor;
            tNext = firstTabG;
            open_wins = 0;
         } else
            ++open_wins;
      }

      // Without the ":tab" modifier only do the current tab
      if (had_tab == 0 || tNext == NULL)
         break;
      gotoTab(tNext, true, true);
    }

   //Go through the book list.  When a book doesn't have a portal yet,
   //open one.  Otherwise move the portal to the right position.
   //Watch out for autocommands that delete books or portals!
   //Don't execute Win/Buf Enter/Leave autocommands here.
   ++autocmd_no_enter;
   enterPortal(lastPor, false);
   ++autocmd_no_leave;
   for (book = firstBook; book && open_wins < count; book = book->next) {
      // Check if this book needs a portal
      if ((!all && book->mem.mfile == NULL) || !book->o.bookListed)
         continue;

      if (had_tab != 0) {
         // With the ":tab" modifier don't move the portal.
         if (book->countPortals > 0)
            po = lastPor;       // book has a portal, skip it
         else
            po = NULL;
      } else {
         // Check if this book already has a portal
         FOR_ALL_PORTALS(po) {
            if (po->book == book)
               break;
         } 
         // If the book already has a portal, move it
         if (po)
            portMoveAfter(po, curPor);
      }

      if (!po && split_ret == OK) {
         BookRef bookRef;

         bookStoreInRef(OUT &bookRef, book);

         // Split the portal and put the book in it
         p_ea_save = p_ea;
         p_ea = true;      // use space from all portals
         split_ret = splitPortal(0, WSP_ROOM | WSP_BELOW);
         ++open_wins;
         p_ea = p_ea_save;
         if (split_ret == FAIL)
            continue;

         // Open this portal into the book
         swap_exists_action = SEA_DIALOG;
         bookSetCurBook(book, DOBOOK_GOTO);
         if (!bookRefValid(&bookRef)) {
            // autocommands deleted the book!!!
            swap_exists_action = SEA_NONE;
            break;
         }
         if (swap_exists_action == SEA_QUIT) {
            Cleanup   cs;

            //Reset the error/interrupt/exception state here so that
            //aborting() returns false when closing a portal.
            enter_cleanup(&cs);

            // User selected Quit at ATTENTION prompt; close this portal.
            closePortal(curPor, true);
            --open_wins;
            swap_exists_action = SEA_NONE;
            swap_exists_did_quit = true;

            // Restore the error/interrupt/exception state if not
            // discarded by a new aborting error, interrupt, or uncaught
            // exception.
            leave_cleanup(&cs);
         } else
            handle_swap_exists(NULL);
      }

      ui_breakcheck();
      if (gotInterruptG) {
         (void)vgetc();   // only break the file loading, not the rest
         break;
      }
      //Autocommands deleted the book or aborted script processing!!!
      if (aborting())
         break;
      //When ":tab" was used open a new tab for a new portal repeatedly.
      if (had_tab > 0)
         commModifierG.cmod_tab = 9999;
   }
   --autocmd_no_enter;
   enterPortal(firstPor, false);      // back to first portal
   --autocmd_no_leave;

   // Close superfluous portals.
   for (po = lastPor; open_wins > count; ) {
      if (!portalIsValid(po)) {
         // BufWrite Autocommands made the portal invalid, start over
         po = lastPor;
      } else {
         closePortal(po, false);
         --open_wins;
         po = lastPor;
      }
   }
}

// Return true if "book" is a normal book
pub int
bt_normal(Book* book) {
   return book && book->kind == BOOK_NORMAL;
}

// Return true if "book" is the location list book.
pub Boole
isLocationListBook(Book* book) {
   return book && bookIsValid(book) && book->kind == BOOK_LOCATION;
}

// Return true if "book" is a terminal book.
pub int
bt_terminal(Book* book) {
   return book && book->kind == BOOK_TERMINAL;
}

// Return true if "book" is a help book.
pub int
bookIsHelp(Book* book) {
   return book && book->kind == BOOK_HELP;
}

// Return true if "book" is a prompt book.
pub int
bt_prompt(Book* book) {
   return book && book->kind == BOOK_PROMPT;
}

// Return true if "book" is a book for a popup portal.
pub int
bt_popup(Book* book) {
   return book && book->kind == BOOK_POPUP;
}

//Return true if "book" is a "nofile", "acwrite", "terminal" or "prompt"
//book. This means the book name may not be a file name, at least not for writing the book.
pub int
bt_nofilename(Book* book) {
   return book && (book->kind == BOOK_NOFILE
       || book->kind == BOOK_ACWRITE
       || book->kind == BOOK_TERMINAL
       || book->kind == BOOK_PROMPT
       );
}

// Return true if "book" is a "nofile", "quickfix", "terminal" or "prompt"
// book. This means the book is not to be read from a file.
private int
bt_nofileread(Book* book) {
   return book && (book->kind == BOOK_NOFILE 
       || book->kind == BOOK_TERMINAL
       || book->kind == BOOK_LOCATION
       || book->kind == BOOK_PROMPT
       );
}

// Return true if "book" has 'buftype' set to "nofile".
pub int
bt_nofile(Book* book) {
   return book && book->kind == BOOK_NOFILE;
}

// Return true if "book" is a "nowrite", "nofile", "terminal", "prompt", or "popup" book.
pub Boole
bookDontWrite(Book* book) {
    return book && (book->kind == BOOK_NOWRITE
       || book->kind == BOOK_NOFILE
       || book->kind == BOOK_TERMINAL
       || book->kind == BOOK_PROMPT
       || book->kind == BOOK_POPUP
       );
}

pub int
bookDontWrite_msg(Book* book) {
   if (bookDontWrite(book)) {
      emsg(_(e_cannot_write_buftype_option_is_set));
      return true;
   }
   return false;
}

// Return special book name. Returns NULL when the book has a normal file name.
pub CS
bookSpName(Book* book) {
   if (isLocationListBook(book)) {
      // Differentiate between the quickfix and location list books using
      // the book number stored in the global quickfix stack.
      if (book->fiNum == qf_stack_get_bufnr())
          return (CS)_(msg_qflist);
      else
          return (CS)_(msg_loclist);
   }

   // There is no _file_ when 'buftype' is "nofile", shortFileName
   // contains the name as specified by the user.
   if (bt_nofilename(book)) {
      if (book->term)
          return term_get_status_text(book->term);
      if (book->currFileName)
          return book->currFileName;
      if (book == commPortBookG)
          return (CS)_("[Command Line]");
      if (bt_prompt(book))
          return (CS)_("[Prompt]");
      if (bt_popup(book))
          return (CS)_("[Popup]");
      return (CS)_("[Scratch]");
   }

   if (book->currFileName == NULL)
      return bookGetFname(book);
   return NULL;
}

//Get "book->currFileName", use "[No Name]" if it is NULL.
pub CS
bookGetFname(Book* book) {
   if (book->currFileName == NULL)
      return (CS)_("[No Name]");
   return book->currFileName;
}

//Set 'buflisted' for curBook to "on" and trigger autocommands if it changed.
pub void
bookSetBooklisted(Boole on) {
   if (on == curBook->o.bookListed)
      return;
   curBook->o.bookListed = on;
   if (on)
      applyAutocomms(EVENT_BUFADD, NULL, NULL, false, curBook);
   else
      applyAutocomms(EVENT_BUFDELETE, NULL, NULL, false, curBook);
}

//Read the file for "book" again and check if the contents changed.
//Return true if it changed or this could not be checked.
pub Boole
bookContentsChanged(Book* book){
   // Allocate a book without putting it in the book list.
   Book* new = bookNew(NULL, NULL, (LineNr)1, BLN_DUMMY);
   if (!new)
      return true;

   // Force the 'binary' option to be equal.
   Invocation invo;
   if (prep_exarg(&invo, book) == FAIL) {
      bookWipe(new, false);
      return true;
   }

   // Set curPor/curBook to book and save a few things.
   AutocommSave aco;
   auCommPrepareBook(&aco, new);
   if (curBook != new) {
      // Failed to find a portal for "new".
      bookWipe(new, false);
      return true;
   }

   //We don't want to trigger autocommands now, they may have nasty side-effects like wiping books
   block_autocmds();
   Boole differ = true;
   if (ml_open(curBook) == OK
       && readfile(
             book->fullFileName, book->currFileName, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM,
             &invo, READ_NEW | READ_DUMMY
          ) == OK
   ) {
      // compare the two files line by line
      if (book->mem.lineCount == curBook->mem.lineCount) {
         differ = false;
         for (LineNr lnum = 1; lnum <= curBook->mem.lineCount; ++lnum) {
            if (STRCMP(memGetLine(book, lnum, false), ml_get(lnum)) != 0) {
                differ = true;
                break;
            }
         } 
      }
   }
   eeglFree(invo.comm);

   // restore curPor/curBook and a few other things
   auCommRestoreBook(&aco);

   if (curBook != new)   // safety check
      bookWipe(new, false);

   unblock_autocmds();

   return differ;
}

// Wipe out a book and decrement the last book number if it was used for
// this book.  Call this to wipe out a temp book that does not contain any marks.
pub void
bookWipe(Book* book, int aucmd) { // When true, trigger autocommands.
   if (book->fiNum == top_file_num - 1)
      --top_file_num;

   if (aucmd == 0)          // Don't trigger BufDelete autocommands here.
      block_autocmds();

   bookClose(NULL, book, DOBOOK_WIPE, false, true);

   if (!aucmd)
      unblock_autocmds();
}

//Output a string for the version message.  If it's going to wrap, output a
//newline, unless the message is too long to fit on the screen anyway.
pub void
printMsgWithWrap(CS s) {
   int len = eeglStrSize(s);

   if (!gotInterruptG && len < (int)visibleColsG 
         && msgColG + len >= (int)visibleColsG && *s != '\n'
   )
      msg_putchar('\n');
   if (!gotInterruptG) {
      msg_puts(s);
   }
}

//List string items nicely aligned in columns. When "size" is < 0 then the last entry is marked 
//with NULL. The entry with index "current" is inclosed in [].
private void
listInColumns(Arr(CS) items, int size, int current, Boole useHilite) {
   Unt cur_row = 1;
   Unt itemCount = 0;
   int width = 0;

   // Find the length of the longest item, use that + 1 as the column width.
   for (int i = 0; size < 0 ? items[i] != NULL : i < size; ++i) {
      int l = eeglStrSize(items[i]) + (i == current ? 2 : 0);

      if (l > width)
         width = l;
      ++itemCount;
   }
   width += 1;

   if (visibleColsG < width) {
      // Not enough screen columns - show one per line
      for (Unt i = 0; i < itemCount; ++i) {
         printMsgWithWrap(items[i]);
         if (msgColG > 0 && i + 1 < itemCount)
            msg_putchar('\n');
      }
      return;
   }

   //The rightmost column doesn't need a separator.
   //Sacrifice it to fit in one more column if possible.
   Unt ncol = (Unt) (visibleColsG + 1) / width;
   Unt nrow = itemCount / ncol + ((itemCount % ncol) ? 1 : 0);

   //"i" counts columns then rows. "idx" counts rows then columns.
   for (Unt i = 0; !gotInterruptG && i < nrow * ncol; ++i) {
      Unt idx = (i / ncol) + (i % ncol) * nrow;
      if (idx < itemCount) {
         int last_col = (i + 1) % ncol == 0;

         if (idx == (Unt)current)
            msg_putchar('[');
         if (useHilite && items[idx][0] == '-')
            msgPutsDeco(items[idx], getDecoFlags(HLF_W));
         else
            msg_puts(items[idx]);
         if (idx == (Unt)current)
            msg_putchar(']');
         if (last_col) {
            if (msgColG > 0 && cur_row < nrow)
               msg_putchar('\n');
            ++cur_row;
         } else {
            while (msgColG % width)
               msg_putchar(' ');
         }
      } else {
         // this row is out of items, thus at the end of the row
         if (msgColG > 0) {
            if (cur_row < nrow)
               msg_putchar('\n');
            ++cur_row;
         }
      }
   }
}

//Compare functions for qsort() below, that compares lastUsed.
pub int
bookCompare(const void* s0, const void* s1) {
   Book *book0 = *(Book **)s0;
   Book *book1 = *(Book **)s1;

   if (book0->lastUsed == book1->lastUsed)
      return 0;
   return book0->lastUsed > book1->lastUsed ? -1 : 1;
}

//Check if the 'modified' flag is set. "nofile" and "scratch" type buffers are 
//considered to always be unchanged. Also considers a buffer changed when a terminal portal 
//contains a running job.
pub Boole
bookWasChanged(Book* book) {
   if (term_job_running_not_none(book->term))
      return true;
   return bookWasChangedNotTerm(book);
}

//Like bookWasChanged() but ignoring a terminal portal.
pub Boole
bookWasChangedNotTerm(Book* book) {
   //In a "prompt" book we do respect 'modified', so that we can control
   //closing the portal by setting or resetting that option.
   return (!bookDontWrite(book) || bt_prompt(book)) && book->wasModified;
}

//Get book name for "book" into nameBuffG[].
//Take care of special book names and translate special characters.
pub void
drawGetTranslatedBookName(Book* book) {
   if (bookSpName(book))
      copySubstrToAllocation(nameBuffG, (Text){bookSpName(book), MAXPATHL - 1});
   ei (book && book->kind == BOOK_HELP) { 
      strPrintShortName(book->currFileName, nameBuffG, MAXPATHL);
   } else
      home_replace(book->currFileName, nameBuffG, MAXPATHL, true);
   trans_characters(nameBuffG, MAXPATHL);
}

//start editing a new file
//
//  fnum: file number; if zero use fullFName/sfname
//  fullFName: the file name
//     - full path if sfname used,
//     - any file name if sfname is NULL
//     - empty string to re-edit with the same file name (but may be
//         in a different directory)
//     - NULL to start an empty book
//  sfname: the short file name (or NULL)
//  invo: contains the command to be executed after loading the file and
//       forced 'ff' and 'fenc'
//  newlnum: if > 0: put cursor on this line number (if possible)
//       if ECMD_LASTL: use last position in loaded file
//       if ECMD_LAST: use last position in all files
//       if ECMD_ONE: use first line
//   flags:
//  ECMD_HIDE: if true don't free the current book
//  ECMD_SET_HELP: set kind = BOOK_HELP for (new) book before opening file
//  ECMD_OLDBUF: use existing book if it exists
//  ECMD_FORCEIT: ! used for a command
//  ECMD_ADDBUF: don't edit, just add to book list
//  ECMD_ALTBUF: like ECMD_ADDBUF and also set the alternate file
//  ECMD_NOWINENTER: Do not trigger BufWinEnter
//  ECMD_MODIFIABLE: set the @modifiable flag for the new book
//  oldPort: Should be "curPor" when editing a new book in the current
//       portal, NULL when splitting the portal first.  When not NULL info
//       of the previous book for "oldPort" is stored.
//
//return FAIL for failure, OK otherwise
pub int
startEditingFile(
   int fnum,
   CS fullFName,
   CS sfname,
   Invocation* invo,         // can be NULL!
   LineNr newlnum,
   Unt flags,
   Portal* oldPort
) {
   if (portErrorIfTermPopup())
      return FAIL;
      
   int oldbuf;           // true if using existing book
   int auto_buf = false; // true if autocommands brought us into the book unexpectedly
   CS new_name = NULL;
   int did_set_swapcommand = false;
   Book* book;
   BookRef bookRef;
   BookRef curBookSaved;
   CS free_fname = NULL;
   int retval = FAIL;
   long n;
   Pos orig_pos;
   LineNr topline = 0;
   int newcol = -1;
   int solcol = -1;
   Pos* pos;
   CS command = NULL;
   Unt readfile_flags = 0;
   int did_inc_redrawing_disabled = false;
   long* so_ptr = &curPor->o.scrollOff;
      
   Unt modifiable = flags & ECMD_MODIFIABLE; 

   if (invo)
      command = invo->higherOrderComm;
   bookStoreInRef(OUT &curBookSaved, curBook);

   Boole sameFile;      // true if editing another file
   if (fnum != 0) {
      if (fnum == curBook->fiNum)   // file is already being edited
         return OK;         // nothing to do
      sameFile = false;
   } else {
      // if no short name given, use fullFName for short name
      if (!sfname)
         sfname = fullFName;

      if ((flags & (ECMD_ADDBUF | ECMD_ALTBUF)) && (fullFName == NULL || *fullFName == ZERO))
         goto theend;

      if (fullFName == NULL)
         sameFile = false; // there is no file name
      ei (*fullFName == ZERO && curBook->fullFileName == NULL)
         sameFile = true;
      else {
         if (*fullFName == ZERO)  {        // re-edit with same file name
            fullFName = curBook->fullFileName;
            sfname = curBook->currFileName;
         }
         free_fname = fiExpandAndCopy(fullFName, true); // may expand to full path name
         if (free_fname)
            fullFName = free_fname;
         sameFile = fNameMatchesCurBook(fullFName);
      }
   }

   //If the file was changed we may not be allowed to abandon it:
   //- if we are going to re-edit the same file
   //- or if we are the only portal into this file and if ECMD_HIDE is false
   if (((sameFile && !(flags & ECMD_OLDBUF))
             || (curBook->countPortals == 1
            && !(flags & (ECMD_HIDE | ECMD_ADDBUF | ECMD_ALTBUF)))
       )
       && check_changed(
             curBook, (p_awa ? CCGD_AW : 0)
             | (sameFile ? CCGD_MULTWIN : 0)
             | ((flags & ECMD_FORCEIT) ? CCGD_FORCEIT : 0)
             | (invo == NULL ? 0 : CCGD_EXCMD)
          )
   ) {
      if (fnum == 0 && !sameFile && fullFName)
         (void)setaltfname(fullFName, sfname, newlnum < 0 ? 0 : newlnum);
      goto theend;
   }

   //End Visual mode before switching to another book, so the text can be copied into the 
   //selection book. Careful: may trigger ModeChanged() autocommand
   //Should we block autocommands here?
   reset_VIsual();

   //autocommands freed portal :(
   if (oldPort && !portalIsValid(oldPort))
      oldPort = NULL;

   if ((command || newlnum > (LineNr)0) && *get_EeglVar_str(VV_SWAPCOMMAND) == ZERO) {
      // Set v:swapcommand for the SwapExists autocommands.
      Unt len = command ? STRLEN(command) + 3 : 30;
      CS p = alloc(len);
      if (command)
         eeSnprintf(p, len, ":%s\r", command);
      else
         eeSnprintf(p, len, "%ldG", (long)newlnum);
      set_EeglVar_string(VV_SWAPCOMMAND, p, -1);
      did_set_swapcommand = true;
      eeglFree(p);
   }

   //If we are starting to edit another file, open a (new) book.
   //Otherwise we re-use the current book.
   if (!sameFile) {
      int prev_alt_fnum = curPor->altFnum;

      if (!(flags & (ECMD_ADDBUF | ECMD_ALTBUF))) {
         if ((commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
            curPor->altFnum = curBook->fiNum;
         if (oldPort)
            bookSetPosInPort(curBook, oldPort, oldPort->cursor.lnum, oldPort->cursor.col, true);
      }

      if (fnum) {
         book = bookFindFileByBookNr(fnum);
      } else {
         if (flags & (ECMD_ADDBUF | ECMD_ALTBUF)) {
            //Default the line number to zero to avoid that a wininfo item
            //is added for the current portal.
            LineNr tlnum = 0;

            if (command) {
               tlnum = atol((char *)command);
               if (tlnum <= 0)
                  tlnum = 1L;
            }
            //Add BLN_NOCURWIN to avoid a new wininfo items are associated
            //with the current portal.
            Book* newbuf = bookNew(
               fullFName, sfname, tlnum, modifiable | BLN_LISTED | BLN_NOCURWIN
            );
            if (newbuf) {
               if (flags & ECMD_ALTBUF)
                  curPor->altFnum = newbuf->fiNum;
               if (tlnum > 0)
                  newbuf->lastCursor.lnum = tlnum;
            }
            goto theend;
         }
         book = bookNew(
            fullFName, sfname, 0L, 
            modifiable | BLN_CURBOOK | ((flags & ECMD_SET_HELP) ? 0 : BLN_LISTED)
         );
             
         // autocommands may change curPor and curBook
         if (oldPort)
            oldPort = curPor;
         bookStoreInRef(OUT &curBookSaved, curBook);
      }
      if (!book)
         goto theend;
      // autocommands try to edit a closing book, which like splitting, can
      // result in more portal displaying it; abort
      if (book->lockedSplit) {
         // portal was split, but not editing the new book, reset countPortals again
         if (oldPort == NULL && curPor->book != NULL && curPor->book->countPortals > 1)
            --curPor->book->countPortals;
         emsg(_(e_cannot_switch_to_a_closing_buffer));
         goto theend;
      }
      if (curPor->altFnum == book->fiNum && prev_alt_fnum != 0)
         // reusing the book, keep the old alternate file
         curPor->altFnum = prev_alt_fnum;

      if (book->mem.mfile == NULL) {    // no memfile yet
         oldbuf = false;
      } else {              // existing memfile
         oldbuf = true;
         bookStoreInRef(OUT &bookRef, book);
         (void)bookCheckTimestamp(book);
         // Check if autocommands made the book invalid or changed the current book.
         if (!bookRefValid(&bookRef) || curBook != curBookSaved.c)
            goto theend;
         if (aborting())       // autocmds may abort script processing
            goto theend;
      }

      // May jump to last used line number for a loaded book or when asked for explicitly
      if ((oldbuf && newlnum == ECMD_LASTL) || newlnum == ECMD_LAST) {
         pos = bookFindFpos(book);
         newlnum = pos->lnum;
         solcol = pos->col;
      }

      //Make the (new) book the one used by the current portal.
      //If the old book becomes unused, free it if ECMD_HIDE is false.
      //If the current buffer was empty and has no file name, curBook
      //is returned by bookNew(), nothing to do here.
      if (book != curBook) {
         BookRef save_auNewCurBuf;
         int save_commPortTypeG = commPortTypeG;
         Portal* save_commPortPortG = commPortPortG;

         // Should only be possible to get here if the commporta is closed, or
         // if it's opening and its buffer hasn't been set yet (the new buffer is for it).
         assert(commPortBookG == NULL);

         // BufLeave applies to the old buffer.
         commPortTypeG = 0;
         commPortPortG = NULL;

         //Be careful: The autocommands may delete any buffer and change the current buffer.
         //- If the buffer we are going to edit is deleted, give up.
         //- If the current buffer is deleted, prefer to load the new buffer when loading a 
         //  buffer is required. This avoids loading another buffer which then must be closed again.
         //- If we ended up in the new buffer already, need to skip a few things, set auto_buf.
         if (book->currFileName != NULL)
            new_name = copyStr(book->currFileName);
         save_auNewCurBuf = auNewCurBookG;
         bookStoreInRef(OUT &auNewCurBookG, book);
         applyAutocomms(EVENT_BUFLEAVE, NULL, NULL, false, curBook);

         commPortTypeG = save_commPortTypeG;
         commPortPortG = save_commPortPortG;

         if (!bookRefValid(&auNewCurBookG)) {
            // new buffer has been deleted
            deleteMsg(new_name);   // frees new_name
            auNewCurBookG = save_auNewCurBuf;
            goto theend;
         }
         if (aborting()) {     // autocmds may abort script processing
            eeglFree(new_name);
            auNewCurBookG = save_auNewCurBuf;
            goto theend;
         }
         if (book == curBook)      // already in new buffer
            auto_buf = true;
         else {
            Portal* the_curPor = curPor;
            int did_decrement;
            Book* was_curbuf = curBook;

            //Set the locked flag to avoid that autocommands close the
            //portal. And set locked for the same reason.
            the_curPor->locked = true;
            ++book->locked;

            if (curBook == curBookSaved.c)
               optsCopyToBook(book, BCO_ENTER);

            // Close the link to the current buffer. This will set oldPort->buffer to NULL.
            u_sync(false);
            did_decrement = bookClose(oldPort, curBook,
                (flags & ECMD_HIDE) ? 0 : DOBOOK_UNLOAD, false, false);

            // Autocommands may have closed the portal.
            if (portalIsValid(the_curPor))
               the_curPor->locked = false;
            --book->locked;

            // autocmds may abort script processing
            if (aborting() && curPor->book != NULL) {
               eeglFree(new_name);
               auNewCurBookG = save_auNewCurBuf;
               goto theend;
            }
            // Be careful again, like above.
            if (!bookRefValid(&auNewCurBookG)) {
               // new buffer has been deleted
               deleteMsg(new_name);   // frees new_name
               auNewCurBookG = save_auNewCurBuf;
               goto theend;
            }
            if (book == curBook) {    // already in new buffer
               // bookClose() has decremented the portal count,
               // increment it again here and restore buffer.
               if (did_decrement && bookIsValid(was_curbuf))
                  ++was_curbuf->countPortals;
               if (doesPortalExistInAnyTab(oldPort) && !oldPort->book)
                  oldPort->book = was_curbuf;
               auto_buf = true;
            } else {
               // <VN> We could instead free the synblock and re-attach to buffer, perhaps.
               if (!curPor->book || curPor->ownSyntax == &(curPor->book->syntax))
                  curPor->ownSyntax = &(book->syntax);
               curPor->book = book;
               curBook = book;
               ++curBook->countPortals;

               // Set 'binary' when forced.
               if (!oldbuf && invo != NULL) {
                  set_file_options(invo);
               }
            }

            //May get the portal options from the last time this buffer was in this portal (or 
            //another portal). If not used before, reset the local window options to the global
            //values. Also restores old folding stuff.
            get_winopts(curBook);
         }
         eeglFree(new_name);
         auNewCurBookG = save_auNewCurBuf;
      }

      curPor->prevContextMark.lnum = 1;
      curPor->prevContextMark.col = 0;
   } else { // sameFile
      if ((flags & (ECMD_ADDBUF | ECMD_ALTBUF)) != 0 || check_fname() == FAIL)
         goto theend;
      oldbuf = (flags & ECMD_OLDBUF);
   }

   // Don't redraw until the cursor is in the right line, otherwise
   // autocommands may cause ml_get errors.
   ++isRedrawingDisabledG;
   did_inc_redrawing_disabled = true;

   book = curBook;
   if ((flags & ECMD_SET_HELP) || keep_help_flag) {
      prepare_help_buffer();
   } else {
      // Don't make a buffer listed if it's a help buffer.  Useful when
      // using CTRL-O to go back to a help file.
      if (curBook->kind != BOOK_HELP)
         bookSetBooklisted(true);
   }

   // If autocommands change buffers under our fingers, forget about editing the file.
   if (book != curBook)
      goto theend;
   if (aborting())       // autocmds may abort script processing
      goto theend;

   //Since we are starting to edit a file, consider the filetype to be
   //unset.  Helps for when an autocommand changes files and expects syntax
   //highlighting to work in the other file.
   curBook->didFiletype = false;

   //sameFile   oldbuf
   // true    false      re-edit same file, buffer is re-used
   // true    true       re-edit same file, nothing changes
   // false   false      start editing new file, new buffer
   // false   true       start editing in existing buffer (nothing to do)
   if (sameFile && !oldbuf) {    //re-use the buffer
      set_last_cursor(curPor);   //may set lastCursor
      if (newlnum == ECMD_LAST || newlnum == ECMD_LASTL) {
         newlnum = curPor->cursor.lnum;
         solcol = curPor->cursor.col;
      }
      book = curBook;
      if (book->currFileName)
         new_name = copyStr(book->currFileName);
      else
         new_name = NULL;
      bookStoreInRef(OUT &bookRef, book);

      // If the buffer was used before, store the current contents so that
      // the reload can be undone.  Do not do this if the (empty) buffer is
      // being re-used for another file.
      if (!(curBook->flags & BF_NEVERLOADED) && (p_ur < 0 || curBook->mem.lineCount <= p_ur)) {
         // Sync first so that this is a separate undo-able action.
         u_sync(false);
         if (u_savecommon(0, curBook->mem.lineCount + 1, 0, true) == FAIL) {
            eeglFree(new_name);
            goto theend;
         }
         u_unchanged(curBook);
         bookFreeAll(curBook, BFA_KEEP_UNDO);

         // tell readfile() not to clear or reload undo info
         readfile_flags = READ_KEEP_UNDO;
      } else
         bookFreeAll(curBook, 0);   // free all things for buffer

      // If autocommands deleted the buffer we were going to re-edit, give
      // up and jump to the end.
      if (!bookRefValid(&bookRef)) {
         deleteMsg(new_name);   // frees new_name
         goto theend;
      }
      eeglFree(new_name);

      // If autocommands change buffers under our fingers, forget about
      // re-editing the file.  Should do the buf_clear_file(), but perhaps
      // the autocommands changed the buffer...
      if (book != curBook)
         goto theend;
      if (aborting())       // autocmds may abort script processing
         goto theend;
      buf_clear_file(curBook);
      curBook->opStart.lnum = 0;   // clear '[ and '] marks
      curBook->opEnd.lnum = 0;
    }

   // If we got here we are sure to start editing Assume success now
   retval = OK;

   // If the file name was changed, reset the not-edit flag so that ":write" works.
   if (sameFile)
      curBook->flags &= ~BF_NOTEDITED;

   //Check if we are editing the argListInd file in the argument list.
   check_arg_idx(curPor);

   if (!auto_buf) {
      //Set cursor and init portal before reading the file and executing
      //autocommands.  This allows for the autocommands to position the cursor.
      curPor_init();

      // It's possible that all lines in the buffer changed.  Need to update
      // automatic folding for all windows where it's used.
      {
         Portal* port;
         Tab* t;
         FOR_ALL_TAB_PORTALS(t, port) {
            if (port->book == curBook)
               foldUpdateAll(port);
         } 
      }

      // Change directories when the 'acd' option is set.
      DO_AUTOCHDIR;

      //Careful: bookOpenFromInvo() and applyAutocomms() may change the current buffer and portal
      orig_pos = curPor->cursor;
      topline = curPor->topLine;
      if (!oldbuf) {    // need to read the file
         // Don't use the swap-exists dialog for a popup window, can't edit the buffer.
         if (PORTAL_IS_POPUP(curPor))
            curBook->flags |= BF_NO_SEA;
         swap_exists_action = SEA_DIALOG;
         curBook->flags |= BF_CHECK_RO; // set/reset 'ro' flag

         //Open the buffer and read the file.
         if (flags & ECMD_NOWINENTER)
            readfile_flags |= READ_NOWINENTER;
         if (should_abort(bookOpenFromInvo(false, invo, readfile_flags)))
            retval = FAIL;

         curBook->flags &= ~BF_NO_SEA;
         if (swap_exists_action == SEA_QUIT)
            retval = FAIL;
         handle_swap_exists(&curBookSaved);
      } else {
         applyAutocommsRetval(EVENT_BUFENTER, NULL, NULL, false, curBook, &retval);
         if ((flags & ECMD_NOWINENTER) == 0)
            applyAutocommsRetval(EVENT_BUFWINENTER, NULL, NULL, false, curBook, &retval);
      }
      check_arg_idx(curPor);

      // If autocommands change the cursor position or topline, we should
      // keep it.  Also when it moves within a line. But not when it moves to the first non-blank.
      if (!EQUAL_POS(curPor->cursor, orig_pos)) {
         CS text = ml_get_curline();

         if (curPor->cursor.lnum != orig_pos.lnum
              || curPor->cursor.col != (int)(skipwhite(text) - text)) {
            newlnum = curPor->cursor.lnum;
            newcol = curPor->cursor.col;
         }
      }
      if (curPor->topLine == topline)
          topline = 0;

      // Even when cursor didn't move we need to recompute topline.
      changed_line_abv_curs();

      if (PORTAL_IS_POPUP(curPor) && curPor->isPreview && retval != FAIL)
         setPopupTitle(curPor);
   }

   // Tell the diff stuff that this buffer is new and/or needs updating. Also needed when 
   // re-editing the same buffer, because unloading will have removed it as a diff buffer.
   if (curPor->o.diff) {
      diffAddBook(curBook);
      diff_invalidate(curBook);
   }

   if (command == NULL) {
      if (newcol >= 0) {  // position set by autocommands
          curPor->cursor.lnum = newlnum;
          curPor->cursor.col = newcol;
          check_cursor();
      } ei (newlnum > 0) {  // line number from caller or old position
          curPor->cursor.lnum = newlnum;
          check_cursor_lnum();
          if (solcol >= 0 && !p_sol) {
            // 'sol' is off: Use last known column.
            curPor->cursor.col = solcol;
            check_cursor_col();
            curPor->cursor.coladd = 0;
            curPor->setCursWant = true;
         }
         else
            beginline(BL_SOL | BL_FIX);
      } else {        // no line number
         beginline(BL_WHITE | BL_FIX);
      }
   }

   // Check if cursors in other portals into the same buffer are still valid
   check_lnums(false);

   //Did not read the file, need to show some info about the file. Do this after setting the cursor
   if (oldbuf && !auto_buf) {
      int msg_scroll_save = msg_scroll;

      //Obey the 'O' flag in 'cpoptions': overwrite any previous file message.
      if (!isExitingG && p_verbose == 0)
         msg_scroll = false;
      if (!msg_scroll)   // wait a bit when overwriting an error msg
         check_for_delay(false);
      msg_start();
      msg_scroll = msg_scroll_save;
      msg_scrolled_ign = true;

      fileinfo(false, true, false);

      msg_scrolled_ign = false;
   }

   curBook->lastUsed = eeTime();

   if (command)
      doCommand(command, NULL, NULL, DOCMD_VERBOSE);

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   did_inc_redrawing_disabled = false;
   if (!skip_redraw) {
      n = *so_ptr;
      if (topline == 0 && !command)
         *so_ptr = 9999;      // force cursor halfway the portal
      update_topline();
      curPor->scbindPos = curPor->topLine;
      *so_ptr = n;
      drawCurBookLater(UPD_NOT_VALID);   // redraw this buffer later
   }

theend:
   if (did_inc_redrawing_disabled && isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   if (did_set_swapcommand)
      set_EeglVar_string(VV_SWAPCOMMAND, NULL, -1);
   eeglFree(free_fname);
   return retval;
}

private void
deleteMsg(CS name) {
   showErrFmtMsg(_(e_autocommands_unexpectedly_deleted_new_buffer_str), name == NULL ? (CS)"" : name);
   eeglFree(name);
   auNewCurBookG.c = NULL;
   auNewCurBookG.freeCount = 0;
}

pub void
c_swapname(Invocation*) {
   if (curBook->mem.mfile == NULL || curBook->mem.mfile->fName == NULL)
      msg(_("No swap file"));
   else
      msg(curBook->mem.mfile->fName);
}

pub Boole
bookNoMemfile(Book* b) {
   return !b->mem.mfile;
}

pub Boole
bookNoFname(Book* book) {
   return (!book || book->mem.mfile || !book->mem.mfile->fName);
}

pub CS
bookGetSwapName(Book* book) {
   return book->mem.mfile->fName;
}

// "swapname(expr)" function
pub void
f_swapname(Arr(Var) argvars, Var* returnVar) {
   returnVar->tag = VAR_STRING;

   Book* book = daGetBook(&argvars[0], false);
   if (!book || book->mem.mfile || !book->mem.mfile->fName)
      returnVar->string = NULL;
   else
      returnVar->string = copyStr(book->mem.mfile->fName);
}

//Check if book "book" has been changed.
//Also check if the file for a new book unexpectedly appeared.
//return 1 if a changed book was found.
//return 2 if a message has been displayed.
//return 0 otherwise.
pub int
bookCheckTimestamp(Book* book){
   FileStat st;
   int stat_res;
   int retval = 0;
   CS mesg = NULL;
   CS mesg2 = S"";
   int helpmesg = false;
   enum {
      RELOAD_NONE,
      RELOAD_NORMAL,
      RELOAD_DETECT
   } reload = RELOAD_NONE;
   int can_reload = false;
   FileOffset orig_size = book->origSize;
   int orig_mode = book->origMode;
   static int busy = false;
   int n;
   BookRef bufref;

   bookStoreInRef(OUT &bufref, book);

   // If there is no file name, the book is not loaded, 'buftype' is
   // set, we are in the middle of a save or being called recursively: ignore this book.
   if (!book->fullFileName
       || !book->mem.mfile
       || !bt_normal(book)
       || book->isBeingSaved
       || busy
       || book->term
   )
      return 0;

   if (   (book->flags & BF_NOTEDITED) != 0
       && book->modifiedTime != 0
       && ((stat_res = stat((char *)book->fullFileName, &st)) < 0
            || time_differs(&st, book->modifiedTime, book->modifiedTimeNs)
            || st.st_size != book->origSize
            || (int)st.st_mode != book->origMode
      )
   ) {
      long prev_modifiedTime = book->modifiedTime;

      retval = 1;

      // set modifiedTime to stop further warnings (e.g., when executing FileChangedShell autocmd)
      if (stat_res < 0) {
         // Check the file again later to see if it re-appears.
         book->modifiedTime = -1;
         book->origSize = 0;
         book->origMode = 0;
      } else
         buf_store_time(book, &st, book->fullFileName);

      // Don't do anything for a directory.  Might contain the file explorer.
      if (mch_isdir(book->currFileName))
          ;
      ei (!bookWasChanged(book) && stat_res >= 0)
         reload = RELOAD_NORMAL;
      else {
         CS reason;
         Unt  reasonlen;

         if (stat_res < 0) {
            reason = S"deleted";
            reasonlen = STRLEN_LITERAL("deleted");
         } ei (bookWasChanged(book)) {
            reason = S"conflict";
            reasonlen = STRLEN_LITERAL("conflict");
         }
         //Check if the file contents really changed to avoid giving a warning when only the 
         //timestamp was set (e.g., checked out of CVS).  Always warn when the buffer was changed.
         ei (orig_size != book->origSize || bookContentsChanged(book)) {
            reason = S"changed";
            reasonlen = STRLEN_LITERAL("changed");
         } ei (orig_mode != book->origMode) {
            reason = S"mode";
            reasonlen = STRLEN_LITERAL("mode");
         } else {
            reason = S"time";
            reasonlen = STRLEN_LITERAL("time");
         }

         //Only give the warning if there are no FileChangedShell autocommands.
         //Avoid being called recursively by setting "busy".
         busy = true;
         set_EeglVar_string(VV_FCS_REASON, reason, (int)reasonlen);
         set_EeglVar_string(VV_FCS_CHOICE, S"", 0);
         ++allBookLock;
         n = applyAutocomms(
               EVENT_FILECHANGEDSHELL, book->currFileName, book->currFileName, false, book
         );
         --allBookLock;
         busy = false;
         if (n) {
            if (!bookRefValid(&bufref))
               emsg(_(e_filechangedshell_autocommand_deleted_buffer));
            CS s = get_EeglVar_str(VV_FCS_CHOICE);
            if (STRCMP(s, "reload") == 0 && *reason != 'd')
               reload = RELOAD_NORMAL;
            ei (STRCMP(s, "edit") == 0)
               reload = RELOAD_DETECT;
            ei (STRCMP(s, "ask") == 0)
               n = false;
            else
               return 2;
         }
         if (!n) {
            if (*reason == 'd') {
               // Only give the message once.
               if (prev_modifiedTime != -1)
                  mesg = _(e_file_str_no_longer_available);
            } else {
               helpmesg = true;
               can_reload = true;
               if (reason[2] == 'n') {
                  mesg = _("W12: Warning: File \"%s\" has changed and the buffer was changed in Eegl as well");
                  mesg2 = _("See \":help W12\" for more info.");
               } ei (reason[1] == 'h') {
                  mesg = _("W11: Warning: File \"%s\" has changed since editing started");
                  mesg2 = _("See \":help W11\" for more info.");
               } ei (*reason == 'm') {
                  mesg = _("W16: Warning: Mode of file \"%s\" has changed since editing started");
                  mesg2 = _("See \":help W16\" for more info.");
               } else {
                  // Only timestamp changed, store it to avoid a warning in check_mtime() later.
                  book->readTime = book->modifiedTime;
                  book->readTimeNs = book->modifiedTimeNs;
               }
            }
         }
      }

   } ei ((book->flags & BF_NEW) != 0 && (book->flags & BF_NEW_W) == 0 
         && eeFexists(book->fullFileName)
   ) {
      retval = 1;
      mesg = _("W13: Warning: File \"%s\" has been created after editing started");
      book->flags |= BF_NEW_W;
      can_reload = true;
   }

   if (mesg) {
      CS path = home_replace_save(book, book->currFileName);
      Boole alreadyWarned = false; 
      if (path) {
         if (!helpmesg)
            mesg2 = S"";
         Unt tbufsize = STRLEN(mesg) + STRLEN(path) + 2 + STRLEN(mesg2) + 1; //+2 for "\n" or "; "
                                                                         // and +1 for ZERO
         CS tbuf = alloc(tbufsize);
         int tbuflen = eeSnprintf(tbuf, tbufsize, mesg, path);
         //Set warningmsg here, before the unimportant and output-specific mesg2 has been appended
         set_EeglVar_string(VV_WARNINGMSG, (CS)tbuf, tbuflen);
         if (can_reload) {
            if (*mesg2 != ZERO)
               eeSnprintf(tbuf + tbuflen, tbufsize - tbuflen, "\n%s", mesg2);
            switch (do_dialog(EE_WARNING, (CS)_("Warning"),
                (CS)tbuf,
                (CS)_("&OK\n&Load File\nLoad File &and Options"),
                1, NULL, true)
            ) {
               case 2:
                  reload = RELOAD_NORMAL;
                  break;
               case 3:
                  reload = RELOAD_DETECT;
                  break;
            }
         } ei(stateG > MODE_NORMAL_BUSY || (stateG & MODE_COMMLINE) || alreadyWarned) {
            if (*mesg2 != ZERO)
               eeSnprintf(tbuf + tbuflen, tbufsize - tbuflen, "; %s", mesg2);
            emsg(tbuf);
            retval = 2;
         } else {
            if (!autocmd_busy) {
               msg_start();
               msgPutsDeco(tbuf, getDecoFlags(HLF_E) + MSG_HIST);
               if (*mesg2 != ZERO)
                  msgPutsDeco(mesg2, getDecoFlags(HLF_W) + MSG_HIST);
               msg_clr_eos();
               (void)msg_end();
               if (emsg_silent == 0 && !in_assert_fails) {
                  out_flush();
                  // give the user some time to think about it
                  ui_delay(1004L, true);

                  // don't redraw and erase the message
                  redrawCommlineG = false;
               }
            }
            alreadyWarned = true;
         }

         eeglFree(tbuf);
         eeglFree(path);
      }
   }

   if (reload != RELOAD_NONE) {
      // Reload the book.
      buf_reload(book, orig_mode, reload == RELOAD_DETECT);
      if (book->o.undoFile && book->fullFileName != NULL) {
         Byte hash[UNDO_HASH_SIZE];
         Book* save_curbuf = curBook;

         // Any existing undo file is unusable, write it now.
         curBook = book;
         u_compute_hash(OUT hash);
         u_write_undo(NULL, false, book, hash);
         curBook = save_curbuf;
      }
   }

   // Trigger FileChangedShell when the file was changed in any way.
   if (bookRefValid(&bufref) && retval != 0) {
      (void)applyAutocomms(
            EVENT_FILECHANGEDSHELLPOST, book->currFileName, book->currFileName, false, book
      );
   } 

   return retval;
}

//Shorten filename of a book.
//When "force" is true: Use full path from now on for files currently being
//edited, both for file name and swap file name.  Try to shorten the file
//names a bit, if safe to do so.
//When "force" is false: Only try to shorten absolute file names.
//For books that have buftype "nofile" or "scratch": never change the file name.
pub void
bookShortenName(Book* book, CS dirname, int force) {
   if (book->currFileName
       && !bt_nofilename(book)
       && !strStartsWithUrl(book->currFileName)
       && (force || !book->shortFileName || !strIsRelative(book->shortFileName))
   ) {
      if (book->shortFileName != book->fullFileName)
         EE_CLEAR(book->shortFileName);
      CS p = shorten_fname(book->fullFileName, dirname);
      if (p) {
         book->shortFileName = copyStr(p);
         book->currFileName = book->shortFileName;
      }
      if (!p || book->currFileName == NULL)
         book->currFileName = book->fullFileName;
   }
}

// Shorten filenames for all books.
pub void
shorten_fnames(Boole force){
   Byte dirname[MAXPATHL];

   mch_dirname(dirname, MAXPATHL);
   Book* book;
   FOR_ALL_BOOKS(book) {
      bookShortenName(book, dirname, force);

      // Always make the swap file name a full path, a "nofile" book may also have a swap file
      mf_fullname(book->mem.mfile);
   }
   status_redraw_all();
   needRedrawTabpanelG = true;
   popup_update_preview_title();
}

pub int
bookGetFd(Book* b) {
   return b->mem.mfile->fd;
}

pub void
bookSetDirtyFlag(Book* book) {
   if (book && book->mem.mfile && book->mem.mfile->mf_dirty == MF_DIRTY_YES_NOSYNC)
      // OK to sync the swap file now
      book->mem.mfile->mf_dirty = MF_DIRTY_YES;
} 


//}}}
//{{{bookwrite: functions for writing a book

#define SMALLBUFSIZE   256   // size of emergency write book

//Call write() to write a number of bytes to the file.
//Return FAIL for failure, OK otherwise.
private int
writeBytes(BwInfo* ip) {
   CS buf = ip->bw_buf;   // data to write
   int len = ip->bw_len;   // length of data

   if (ip->fd < 0)
      // Only checking conversion, which is OK if we get here.
      return OK;

   int wlen = write_eintr(ip->fd, buf, len);
   return (wlen < len) ? FAIL : OK;
}

//Check modification time of file, before writing to it. The size isn't checked, because using 
//a tool like "gzip" takes care of using the same timestamp but can't set the size.
private int
check_mtime(Book* book, FileStat *st) {
   if (book->readTime != 0
        && time_differs(st, book->readTime, book->readTimeNs)
   ) {
      msg_scroll = true;       // don't overwrite messages here
      msg_silent = 0;          // must give this prompt
      // don't use emsg() here, don't want to flush the books
      msgDeco(_("WARNING: The file has been changed since reading it!!!"),
                            getDecoFlags(HLF_E)
      );
      if (ask_yesno((CS)_("Do you really want to write to it"), true) == 'n')
         return FAIL;
      msg_scroll = false;       // always overwrite the file message now
    }
    return OK;
}

private void
updateFileTime(
   CS fname,
   Tyme  atime,      // access time
   Tyme  mtime       // modification time
){
   ProfTime newTimes[2];
   newTimes[0] = (ProfTime){.tv_sec = atime, .tv_nsec = 0};
   newTimes[1] = (ProfTime){.tv_sec = mtime, .tv_nsec = 0};

   //first arg is 0 because the path is absolute. Last arg is just absence of flags
   utimensat(0, (char*)fname, newTimes, 0);
}

pub CS
new_file_message(void) {
   return _("(New)");
}

//Get file name to use for backup file.
//Use the name of the edited file "fname" and an entry in the 'dir' or 'bdir' option "dname".
//- If "dname" is ".", return "fname" (backup file in same dir).
//- If "dname" starts with "./", insert "dname" in "fname" (swap file relative to dir of file).
//- Otherwise, prepend "dname" to the tail of "fname" (swap file in specific dir).
//
//The return value is an allocated string and can be NULL.
private CS
determineBackupFilename(CS fname, CS dname){
   CS t;
   CS retval;

   CS tail = fiGetShortFiName(fname);

   if (dname[0] == '.' && dname[1] == ZERO)
      retval = copyStr(fname);
   ei (dname[0] == '.' && dname[1] == '/') {
      if (tail == fname)       // no path before file name
         retval = concat_fnames(dname + 2, tail, true);
      else {
         int save_char = *tail;
         *tail = ZERO;
         t = concat_fnames(fname, dname + 2, true);
         *tail = save_char;
         if (t == NULL)       // out of memory
            retval = NULL;
         else {
            retval = concat_fnames(t, tail, true);
            eeglFree(t);
         }
      }
   } else
      retval = concat_fnames(dname, tail, true);

   return retval;
}

//bookWrite() - write to file "fname" lines "start" through "end"
//
//We do our own buffering here because fwrite() is so slow.
//
//If "forceit" is true, we don't care for errors when attempting backups.
//In case of an error everything possible is done to restore the original
//file.  But when "forceit" is true, we risk losing it.
//
//When "reset_changed" is true and "append" == false and "start" == 1 and
//"end" == curBook->mem.lineCount, reset curBook->wasModified.
//
//This function must NOT use nameBuffG (because it's called by autowrite()).
//
//return FAIL for failure, NOTDONE for refusing to write, OK otherwise
pub int
bookWrite(
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
) {
   if (IMMUTABLE) { //immutable books may not be saved. They are readonly
      return FAIL;
   }
   
   int fd;
   CS backup = NULL;
   int backup_copy = false; // copy the original file?
   CS s;
   Byte c;
   CS errmsg = NULL;
   int errmsg_allocated = false;
   CS errnum = NULL;
   Byte smallbuf[SMALLBUFSIZE];
   CS backup_ext;
   int bufsize;
   long perm;          // file permissions
   int retval = OK;
   int msg_save = msg_scroll;
   int no_eol = false;       // no end-of-line written
   int device = false;       // writing to a device
   int prev_gotInterruptG = gotInterruptG;
   int file_readonly = false;  // overwritten file is read-only
   int made_writable = false;  // 'w' bit has been set
   // writing everything
   Boole whole = (start == 1 && end == book->mem.lineCount);
   LineNr old_line_count = book->mem.lineCount;
   char flags; // decoration flags
   int write_bin;
   ContextSha256 sha_ctx;
   Unt bkc = book->o.backupCopy;
   Pos orig_start = book->opStart;
   Pos orig_end = book->opEnd;
   
   if (!fname || *fname == ZERO)   // safety check
      return FAIL;
   if (!book->mem.mfile) {
      // This can happen during startup when there is a stray "w" in the vimrc file.
      emsg(_(e_empty_buffer));
      return FAIL;
   }

   // Avoid a crash for a long name.
   if (STRLEN(fname) >= MAXPATHL) {
      emsg(_(e_name_too_long));
      return FAIL;
   }
 
   BwInfo writeInfo = (BwInfo) { .tgt = book }; // for writeBytes()

   //After writing a file changedtick changes but we don't want to display the line.
   ex_no_reprint = true;

   //If there is no file name yet, use the one for the written file.
   //BF_NOTEDITED is set to reflect this (in case the write fails).
   //Don't do this when the write is for a filter command. Don't do this when appending.
   if (book->fullFileName == NULL
       && reset_changed
       && whole
       && book == curBook
       && !bt_nofilename(book)
       && !filtering
       && !append
   ){
      if (set_rw_fname(fname, sfname) == FAIL)
         return FAIL;
      book = curBook;       // just in case autocmds made "book" invalid
   }

   if (!sfname)
      sfname = fname;
   // Use the short file name whenever possible.
   // Avoids problems with networks and when directory names are changed.
   CS fullFName = fname;             // remember full fname
   fname = sfname;

   Boole overwriting = (book->fullFileName && fnamecmp(fullFName, book->fullFileName) == 0);

   if (isExitingG)
      termSetMode(TMODE_COOK);       // when exiting allow typeahead now

   ++no_wait_return;          // don't wait for return yet

   // Set '[ and '] marks to the lines to be written.
   book->opStart.lnum = start;
   book->opStart.col = 0;
   book->opEnd.lnum = end;
   book->opEnd.col = 0;

   {
   AutocommSave   aco;
   int      buf_ffname = false;
   int      buf_sfname = false;
   int      buf_fname_f = false;
   int      buf_fname_s = false;
   int      did_cmd = false;
   Boole nofile_err = false;
   int      empty_memline = (book->mem.mfile == NULL);
   BookRef   bookRef;

   // Apply PRE autocommands. Set curBook to the book to be written.
   // Careful: The autocommands may call bookWrite() recursively!
   if (fullFName == book->fullFileName)
      buf_ffname = true;
   if (sfname == book->shortFileName)
      buf_sfname = true;
   if (fname == book->fullFileName)
      buf_fname_f = true;
   if (fname == book->shortFileName)
      buf_fname_s = true;

   // Set curPor/curBook to book and save a few things.
   auCommPrepareBook(&aco, book);
   if (curBook != book) {
      // Could not find a portal for "book".  Doing more might cause problems, better bail out.
      return FAIL;
   }

   bookStoreInRef(OUT &bookRef, book);

   if (append) {
      if (!(did_cmd = 
               auCommApplyWithInvo(EVENT_FILEAPPENDCMD, sfname, sfname, false, curBook, invo))) {
         if (overwriting && bt_nofilename(curBook))
            nofile_err = true;
         else
            auCommApplyWithInvo(EVENT_FILEAPPENDPRE, sfname, sfname, false, curBook, invo);
      }
   } ei (filtering) {
       auCommApplyWithInvo(EVENT_FILTERWRITEPRE, NULL, sfname, false, curBook, invo);
   } ei (reset_changed && whole) {
      int was_changed = bookWasChanged(curBook);

      did_cmd = auCommApplyWithInvo(EVENT_BUFWRITECMD, sfname, sfname, false, curBook, invo);
      if (did_cmd) {
         if (was_changed && !bookWasChanged(curBook)) {
             //Written everything correctly and BufWriteCmd has reset
             //'modified': Correct the undo information so that an undo now sets 'modified'.
             u_unchanged(curBook);
             u_update_save_nr(curBook);
         }
      } else {
         if (overwriting && bt_nofilename(curBook))
            nofile_err = true;
         else
            auCommApplyWithInvo(EVENT_BUFWRITEPRE, sfname, sfname, false, curBook, invo);
      }
   } else {
      if (!(did_cmd = 
                auCommApplyWithInvo(EVENT_FILEWRITECMD, sfname, sfname, false, curBook, invo))
      ) {
         if (overwriting && bt_nofilename(curBook))
            nofile_err = true;
         else
            auCommApplyWithInvo(EVENT_FILEWRITEPRE, sfname, sfname, false, curBook, invo);
      }
   }

   // restore curPor/curBook and a few other things
   auCommRestoreBook(&aco);

   // In three situations we return here and don't write the file:
   // 1. the autocommands deleted or unloaded the book.
   // 2. The autocommands abort script processing.
   // 3. If one of the "Cmd" autocommands was executed.
   if (!bookRefValid(&bookRef))
      book = NULL;
   if (!book || (!book->mem.mfile && !empty_memline)
            || did_cmd || nofile_err
            || aborting()
   ){
      if (book && (commModifierG.cmod_flags & CMOD_LOCKMARKS)) {
         // restore the original '[ and '] positions
         book->opStart = orig_start;
         book->opEnd = orig_end;
      }

      --no_wait_return;
      msg_scroll = msg_save;
      if (nofile_err) {
         showErrFmtMsg(
            _(e_no_matching_autocommands_for_buftype_str_buffer), p_buftype_values[book->kind]
         );
      } 

      if (nofile_err || aborting() )
         // An aborting error, interrupt or exception in the autocommands.
         return FAIL;
      if (did_cmd) {
         if (!book)
            // The book was deleted.  We assume it was written (can't retry anyway).
            return OK;
         if (overwriting) {
            // Assume the book was written, update the timestamp.
            ml_timestamp(book);
            if (append)
               book->flags &= ~BF_NEW;
            else
               book->flags &= ~BF_WRITE_MASK;
         }
         if (reset_changed && book->wasModified && !append && overwriting)
            // Book still changed, the autocommands didn't work properly.
            return FAIL;
         return OK;
      }
      if (!aborting())
         emsg(_(e_autocommands_deleted_or_unloaded_buffer_to_be_written));
      return FAIL;
   }

   // The autocommands may have changed the number of lines in the file.
   // When writing the whole file, adjust the end.
   // When writing part of the file, assume that the autocommands only
   // changed the number of lines that are to be written (tricky!).
   if (book->mem.lineCount != old_line_count) {
      if (whole)                  // write all
         end = book->mem.lineCount;
      ei (book->mem.lineCount > old_line_count)   // more lines
         end += book->mem.lineCount - old_line_count;
      else {                // less lines
         end -= old_line_count - book->mem.lineCount;
         if (end < start) {
             --no_wait_return;
             msg_scroll = msg_save;
             emsg(_(e_autocommands_changed_number_of_lines_in_unexpected_way));
             return FAIL;
         }
      }
   }

   // The autocommands may have changed the name of the book, which may
   // be kept in fname, fullFName and sfname.
   if (buf_ffname)
      fullFName = book->fullFileName;
   if (buf_sfname)
      sfname = book->shortFileName;
   if (buf_fname_f)
      fname = book->fullFileName;
   if (buf_fname_s)
      fname = book->shortFileName;
   }

   if (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
      // restore the original '[ and '] positions
      book->opStart = orig_start;
      book->opEnd = orig_end;
   }

   msg_scroll = isExitingG; // overwrite previous file message?
   if (!filtering)
      filemess(book, fname, S"", 0);   // show that we are busy
   msg_scroll = false;          // always overwrite the file message now

   CS buffer = tryBigAlloc(WRITEBUFSIZE);
   if (buffer == NULL) { // can't allocate big buffer, use small
                         // one (to be able to write when out of memory)
      buffer = smallbuf;
      bufsize = SMALLBUFSIZE;
   } else
      bufsize = WRITEBUFSIZE;

   // Get information about original file (if there is one).
   FileStat     stOld;
   stOld.st_dev = 0;
   stOld.st_ino = 0;
   perm = -1;
   Boole newfile = false;       // true if file doesn't exist yet
   if (stat((char *)fname, &stOld) < 0)
      newfile = true;
   else {
      perm = stOld.st_mode;
      if (!S_ISREG(stOld.st_mode)) {    // not a file
         if (S_ISDIR(stOld.st_mode)) {
            errnum = (CS)"E502: ";
            errmsg = (CS)_(e_is_a_directory);
            goto fail;
         }
         if (mch_nodetype(fname) != NODE_WRITABLE) {
            errnum = (CS)"E503: ";
            errmsg = (CS)_(e_is_not_file_or_writable_device);
            goto fail;
         }
         //It's a device of some kind (or a fifo) which we can write to
         //but for which we can't make a backup.
         device = true;
         newfile = true;
         perm = -1;
      }
   }

   if (!device && !newfile) {
      // Check if the file is really writable (when renaming the file to
      // make a backup we won't discover it later).
      file_readonly = check_file_readonly(fname, (int)perm);

      if (!forceit && file_readonly) {
          errnum = (CS)"E505: ";
          errmsg = (CS)_(e_is_read_only_add_bang_to_override);
          goto fail;
      }

      // Check if the timestamp hasn't changed since reading the file.
      if (overwriting) {
         retval = check_mtime(book, &stOld);
         if (retval == FAIL)
            goto fail;
      }
   }

   // If 'backupskip' is not empty, don't make a backup for some files.
   Boole dobackup = !(p_bsk && match_file_list(p_bsk, sfname, fullFName));

   // Save the value of gotInterruptG and reset it.  We don't want a previous
   // interruption cancel writing, only hitting CTRL-C while writing should abort it.
   prev_gotInterruptG = gotInterruptG;
   gotInterruptG = false;

   // Mark the book as 'being saved' to prevent changed buffer warnings
   book->isBeingSaved = true;

   //{{{backup
   // If we are not appending or filtering, the file exists, and the
   // 'writebackup', 'backup' or 'patchmode' option is set, need a backup.
   // When 'patchmode' is set also make a backup when appending.
   //
   // Do not make any backup, if 'writebackup' and 'backup' are both switched
   // off.  This helps when editing large files on almost-full disks.
   if (!(append) && !filtering && perm >= 0 && dobackup) {
      FileStat       st;

      if ((bkc & BKC_YES) || append)   // "yes"
          backup_copy = true;
      ei ((bkc & BKC_AUTO)) {   // "auto"
          int i;

          // Don't rename the file when:
          // - it's a hard link
          // - it's a symbolic link
          // - we don't have write permission in the directory
          // - we can't set the owner/group of the new file
          if (stOld.st_nlink > 1
                || lstat((char *)fname, &st) < 0
                || st.st_dev != stOld.st_dev
                || st.st_ino != stOld.st_ino
          ) {
             backup_copy = true;
          } else {
            // Check if we can create a file and set the owner/group to
            // the ones from the original file.
            // First find a file name that doesn't exist yet (use some
            // arbitrary numbers).
            STRCPY(IObuff, fname);
            fd = -1;
            for (i = 4913; ; i += 123) {
               sprintf((char *)fiGetShortFiName(IObuff), "%d", i);
               if (lstat((char *)IObuff, &st) < 0) {
                  fd = open((char *)IObuff, O_CREAT|O_WRONLY|O_EXCL|O_NOFOLLOW, perm);
                  if (fd < 0 && errno == EEXIST)
                      // If the same file name is created by another
                      // process between lstat() and open(), find another
                      // name.
                      continue;
                  break;
               }
            }
            if (fd < 0) { // can't write in directory
                backup_copy = true;
            } else {
               (void)fchown(fd, stOld.st_uid, stOld.st_gid);
               if (stat((char *)IObuff, &st) < 0
                   || st.st_uid != stOld.st_uid
                   || st.st_gid != stOld.st_gid
                   || (long)st.st_mode != perm
               ) {
                  backup_copy = true;
               } 
               // Close the file before removing it
               close(fd);
               mch_remove(IObuff);
            }
         }
      }

      // Break symlinks and/or hardlinks if we've been asked to.
      if ((bkc & BKC_BREAKSYMLINK) || (bkc & BKC_BREAKHARDLINK)) {
         int lstat_res;

         lstat_res = lstat((char *)fname, &st);

         // Symlinks.
         if ((bkc & BKC_BREAKSYMLINK) && lstat_res == 0 && st.st_ino != stOld.st_ino)
            backup_copy = false;

         // Hardlinks.
         if ((bkc & BKC_BREAKHARDLINK)
                && stOld.st_nlink > 1
                && (lstat_res != 0 || st.st_ino == stOld.st_ino)) {
            backup_copy = false;
         } 
      }

      // make sure we have a valid backup extension to use
      backup_ext = p_bex ? p_bex : S".bak";

      if (backup_copy && (fd = open((char *)fname, O_RDONLY | O_EXTRA, 0)) >= 0) {
         int bfd;
         CS po;
         int some_error = false;
         FileStat stNew;
         CS rootname;
         CS p;
         mode_t umask_save;

         CS copybuf = tryBigAlloc(WRITEBUFSIZE + 1);
         if (!copybuf) {
            some_error = true;       // out of memory
            goto nobackup;
         }

         //Try to make the backup in each directory in the 'bdir' option.
         //
         //We may have a writable file that cannot be recreated with a simple open(..., O_CREAT, )
         //e.g:
         // - the directory is not writable,
         // - the file may be a symbolic link,
         // - the file may belong to another user/group, etc.
         //
         //For these reasons, the existing writable file must be truncated
         //and reused. Creation of a backup COPY will be attempted.
         if (!p_bdir)
            goto nobackup;
            
         CS backupDirRemainder = p_bdir; //@backupdir
         while (*backupDirRemainder != ZERO) {
            stNew.st_ino = 0;
            stNew.st_dev = 0;
            stNew.st_gid = 0;

            // Isolate one directory name, using an entry in 'bdir'.
            (void)strCutPathFromListOfPaths(OUT &backupDirRemainder, OUT copybuf, WRITEBUFSIZE, S",");

            p = copybuf + STRLEN(copybuf);
            if (after_pathsep(copybuf, p) && p[-1] == p[-2]
                  // Ends with '//', use full path
                  && (p = memMakePercentSwapName(copybuf, p, fname)) != NULL
            ) {
               backup = fiAppendFileExtension(p, backup_ext, false);
               eeglFree(p);
            }
            rootname = determineBackupFilename(fname, copybuf);
            if (!rootname) {
               some_error = true;       // out of memory
               goto nobackup;
            }

            // Make the backup file name.
            if (!backup)
               backup = fiAppendFileExtension(rootname, backup_ext, false);
            if (!backup) {
               eeglFree(rootname);
               some_error = true;      // out of memory
               goto nobackup;
            }

            // Check if backup file already exists.
            if (stat((char *)backup, &stNew) >= 0) {
               //Check if backup file is same as original file. May happen when 
               //fiAppendFileExtension() gave the same file back. E.g. silly link, or file 
               //name-length reached. If we don't check here, we either ruin the file when copying 
               //or erase it after writing. jw.
               if (stNew.st_dev == stOld.st_dev && stNew.st_ino == stOld.st_ino) {
                  EE_CLEAR(backup);   // no backup file to delete
                  goto endOfName;
               }

               //If we are not going to keep the backup file, don't delete an existing one, 
               //try to use another name. Change one character, just before the extension.
               if (!p_bk) {
                  po = backup + STRLEN(backup) - 1 - STRLEN(backup_ext);
                  if (po < backup)   // empty file name ???
                     po = backup;
                  *po = 'z';
                  while (*po > 'a' && stat((char *)backup, &stNew) >= 0)
                     --*po;
                  // They all exist??? Must be something wrong.
                  if (*po == 'a')
                     EE_CLEAR(backup);
               }
            }
endOfName: 
            eeglFree(rootname);

            // Try to create the backup file
            if (backup) {
               //remove old backup, if present
               mch_remove(backup);
               //Open with O_EXCL to avoid the file being created while
               //we were sleeping (symlink hacker attack?). Reset umask
               //if possible to avoid mch_setperm() below.
               umask_save = umask(0);
               bfd = open((char *)backup, O_WRONLY|O_CREAT|O_EXTRA|O_EXCL|O_NOFOLLOW, perm & 0777);
               (void)umask(umask_save);
               if (bfd < 0)
                  EE_CLEAR(backup);
               else {
                  //Set file protection same as original file, but strip s-bit. Only needed 
                  //if umask() wasn't used above. Try to set the group of the backup same as the
                  //original file. If this fails, set the protection bits for the group same as 
                  //the protection bits for others.
                  if (stNew.st_gid != stOld.st_gid && fchown(bfd, (uid_t)-1, stOld.st_gid) != 0)
                      mch_setperm(backup, (perm & 0707) | ((perm & 07) << 3));
                  mch_copy_xattr(fname, backup);

                  // copy the file.
                  writeInfo.fd = bfd;
                  writeInfo.bw_buf = copybuf;
                  while ((writeInfo.bw_len = fiReadEintr(fd, copybuf, WRITEBUFSIZE)) > 0) {
                     if (writeBytes(&writeInfo) == FAIL) {
                        errmsg = (CS)_(e_cant_write_to_backup_file_add_bang_to_override);
                        break;
                     }
                     ui_breakcheck();
                     if (gotInterruptG) {
                        errmsg = (CS)_(e_interrupted);
                        break;
                     }
                  }

                  if (close(bfd) < 0 && !errmsg)
                     errmsg = (CS)_(e_close_error_for_backup_file_add_bang_to_write_anyway);
                  if (writeInfo.bw_len < 0)
                     errmsg = (CS)_(e_cant_read_file_for_backup_add_bang_to_write_anyway);
                  updateFileTime(backup, stOld.st_atime, stOld.st_mtime);
                  mch_copy_xattr(fname, backup);
                  break;
               }
            }
         }
         
      nobackup:
         close(fd);      // ignore errors for closing read file
         eeglFree(copybuf);

         if (backup == NULL && errmsg == NULL)
            errmsg = (CS)_(e_cannot_create_backup_file_add_bang_to_write_anyway);
         // ignore errors when forceit is true
         if ((some_error || errmsg) && !forceit) {
            retval = FAIL;
            goto fail;
         }
         errmsg = NULL;
      } else {
         CS p;

         //Make a backup by renaming the original file.

         //Form the backup file name - change path/fo.o.h to
         //path/fo.o.h.bak Try all directories in 'backupdir', first one that works is used.
         if (!p_bdir)
            goto finishedParsing;
            
         CS backupDirRemainder = p_bdir;
         while (*backupDirRemainder) {
            //Isolate one directory name and make the backup file name.
            (void)strCutPathFromListOfPaths(&backupDirRemainder, IObuff, IOSIZE, S",");

            p = IObuff + STRLEN(IObuff);
            if (after_pathsep(IObuff, p) && p[-1] == p[-2]) {
               //path ends with '//', use full path
               if ((p = memMakePercentSwapName(IObuff, p, fname)) != NULL) {
                  backup = fiAppendFileExtension(p, backup_ext, false);
                  eeglFree(p);
               }
            } 
            if (!backup) {
               CS rootname = determineBackupFilename(fname, IObuff);
               if (rootname) {
                  backup = fiAppendFileExtension(rootname, backup_ext, false);
                  eeglFree(rootname);
               }
            }

            if (backup) {
               // If we are not going to keep the backup file, don't delete an existing one, 
               // try to use another name. Change one character, just before the extension.
               if (!p_bk && mch_getperm(backup) >= 0) {
                  p = backup + STRLEN(backup) - 1 - STRLEN(backup_ext);
                  if (p < backup)   // empty file name ???
                     p = backup;
                  *p = 'z';
                  while (*p > 'a' && mch_getperm(backup) >= 0)
                     --*p;
                  // They all exist??? Must be something wrong!
                  if (*p == 'a')
                     EE_CLEAR(backup);
               }
            }
            if (backup) {
               // Delete any existing backup and move the current version to the backup. For safety,
               // we don't remove the backup until the write has finished successfully. And if the
               // 'backup' option is set, leave it around.

               // If the renaming of the original file to the backup file works, quit here.
               if (eeRename(fname, backup) == 0)
                  break;

               EE_CLEAR(backup);   // don't do the rename below
            }
         }
      finishedParsing: 
         if (backup == NULL && !forceit) {
            errmsg = (CS)_(e_cant_make_backup_file_add_bang_to_write_anyway);
            goto fail;
         }
      }
   } //}}}

   if (end > book->mem.lineCount)
      end = book->mem.lineCount;
   if (book->mem.flags & ML_EMPTY)
      start = end + 1;

   // If the original file is being overwritten, there is a small chance that
   // we crash in the middle of writing. Therefore the file is preserved now.
   // This makes all block numbers positive so that recovery does not need the original file.
   // Don't do this if there is a backup file and we are exiting.
   if (reset_changed && !newfile && overwriting && !(isExitingG && backup)) {
      ml_preserve(book, false);
      if (gotInterruptG) {
         errmsg = (CS)_(e_interrupted);
         goto fail;
      }
   }

   // Default: write the file directly.  May write to a temp file for multi-byte conversion.
   CS wfname = fname;   // name of file to write to
   
   
#define TRUNC_ON_OPEN 0
   // Open the file "wfname" for writing.
   // We may try to open the file twice: If we can't write to the file
   // and forceit is true we delete the existing file and try to
   // create a new one. If this still fails we may have lost the
   // original file!  (this may happen when the user reached his
   // quotum for number of files).
   // Appending will fail if the file does not exist and forceit is false.
   while ((fd = open((char *)wfname, O_WRONLY | O_EXTRA | (append
            ? (forceit ? (O_APPEND | O_CREAT) : O_APPEND)
            : (O_CREAT | TRUNC_ON_OPEN)), perm < 0 ? 0666 : (perm & 0777))) < 0
   ){
      // A forced write will try to create a new file if the old one
      // is still readonly. This may also happen when the directory
      // is read-only. In that case the mch_remove() will fail.
      if (errmsg == NULL) {
          FileStat   st;

         // Don't delete the file when it's a hard or symbolic link.
         if ((!newfile && stOld.st_nlink > 1) 
               || (lstat((char *)fname, &st) == 0 && (st.st_dev != stOld.st_dev
                || st.st_ino != stOld.st_ino))
         ) {
            errmsg = (CS)_(e_cant_open_linked_file_for_writing);
         } else {
            errmsg = (CS)_(e_cant_open_file_for_writing);
            if (forceit && perm >= 0) {
               // we write to the file, thus it should be marked writable after all
               if (!(perm & 0200))
                  made_writable = true;
               perm |= 0200;
               if (stOld.st_uid != getuid() || stOld.st_gid != getgid())
                  perm &= 0777;
               if (!append)  // don't remove when appending
                  mch_remove(wfname);
               continue;
           }
        }
     }

      {
         FileStat   st;

         // If we failed to open the file, we don't need a backup. Throw it away.  If we moved or 
         // removed the original file try to put the backup in its place.
         if (backup && wfname == fname) {
            if (backup_copy) {
               // There is a small chance that we removed the original, so try to move the copy 
               // in its place. This may not work if the eeRename() fails. In that case we leave
               // the copy around.
               if (stat((char *)fname, &st) < 0)
                  eeRename(backup, fname);
               // if original file does exist throw away the copy
               if (stat((char *)fname, &st) >= 0)
                  mch_remove(backup);
            } else {
                // try to put the original file back
                eeRename(backup, fname);
            }
         }

         // if original file no longer exists give an extra warning
         if (!newfile && stat((char *)fname, &st) < 0)
            end = 0;
      }

      if (wfname != fname)
         eeglFree(wfname);
      goto fail;
   }
   writeInfo.fd = fd;

   {
   FileStat   st;

   // Double check we are writing the intended file before making any changes.
   if (overwriting
         && (!dobackup || backup_copy)
         && fname == wfname
         && perm >= 0
         && fstat(fd, &st) == 0
         && st.st_ino != stOld.st_ino
   ){
       close(fd);
       errmsg = (CS)_(e_file_changed_while_writing);
       goto fail;
   }
   }
   if (!append)
      (void)ftruncate(fd, (off_t)0);

   errmsg = NULL;

   writeInfo.bw_buf = buffer;
   Long nchars = 0;

   // use "++bin", "++nobin" or 'binary'
   if (invo && invo->force_bin != 0)
      write_bin = (invo->force_bin == FORCE_BIN);
   else
      write_bin = book->o.binary;

   writeInfo.bw_start_lnum = start;
   
   Boole write_undo_file = (book->o.undoFile
             && overwriting
             && !append
             && !filtering
             && reset_changed);

   if (write_undo_file)
       // Prepare for computing the hash value of the text.
       sha256_start(&sha_ctx);

   writeInfo.bw_len = bufsize;
   s = buffer;
   
   int len = 0; // main loop of writing data
   LineNr lnum;
   for (lnum = start; lnum <= end; ++lnum) {
      // The next while loop is done once for each character written. Keep it fast!
      CS ptr = memGetLine(book, lnum, false) - 1;
      if (write_undo_file)
         sha256_update(&sha_ctx, ptr + 1, (Unt)(STRLEN(ptr + 1) + 1));
      while ((c = *++ptr) != ZERO) {
         if (c == NL)
            *s = ZERO;      // replace newlines with NULs
         else
            *s = c;
         ++s;
         if (++len != bufsize)
            continue;
         if (writeBytes(&writeInfo) == FAIL) {
            end = 0;      // write error: break loop
            break;
         }
         nchars += bufsize;
         s = buffer;
         len = 0;
         writeInfo.bw_start_lnum = lnum;
      }
      // write failed or last line has no EOL: stop here
      if (end == 0
          || (lnum == end
               && ((write_bin && lnum == book->noEolLnum) || (lnum == book->mem.lineCount)))
      ) {
         ++lnum;         // written the line, count it
         no_eol = true;
         break;
      }
      *s++ = NL;
      if (++len == bufsize && end) {
         if (writeBytes(&writeInfo) == FAIL) {
            end = 0;      // write error: break loop
            break;
         }
         nchars += bufsize;
         s = buffer;
         len = 0;

         ui_breakcheck();
         if (gotInterruptG) {
            end = 0;      // Interrupted, break loop
            break;
         }
      }
   }

   if (len > 0 && end > 0) {
      writeInfo.bw_len = len;
      if (writeBytes(&writeInfo) == FAIL)
         end = 0;          // write error
      nchars += len;
   }

   //If we started writing, finish writing. Also when an error was encountered.
   //On many journaling file systems there is a bug that causes both the original and the 
   //backup file to be lost when halting the system right after writing the file. That's 
   //because only the meta-data is journalled. Syncing the file slows down the system, but 
   //assures it has been written to disk and we don't lose it. For a device do try the fsync() 
   //but don't complain if it does not work (could be a pipe). If the @fsync option is off, 
   //don't fsync(). Useful for laptops.
   if (p_fs && eeFsync(fd) != 0 && !device) {
      errmsg = (CS)_(e_fsync_failed);
      end = 0;
   }

   //Probably need to set the security context.
   if (!backup_copy) {
       mch_copy_xattr(backup, wfname);
   }

   //When creating a new file, set its owner/group to that of the
   //original file. Get the new device and inode number.
   if (backup && !backup_copy) {
      FileStat   st;

      //Don't change the owner when it's already OK, some systems remove permission or ACL stuff.
      if (stat((char *)wfname, &st) < 0
          || st.st_uid != stOld.st_uid
          || st.st_gid != stOld.st_gid
      ) {
         //changing owner might not be possible
         (void)fchown(fd, stOld.st_uid, -1);
         //if changing group fails clear the group permissions
         if (fchown(fd, -1, stOld.st_gid) == -1 && perm > 0)
            perm &= ~070;
      }
      buf_setino(book);
   } ei (!book->isDevNumValid) {
      //Set the inode when creating a new file.
      buf_setino(book);
   } 

   if (made_writable)
      perm &= ~0200;   // reset 'w' bit for security reasons
   // set permission of new file same as old file
   if (perm >= 0)
      (void)mch_fsetperm(fd, perm);
   if (close(fd) != 0) {
      errmsg = (CS)_(e_close_failed);
      end = 0;
   }

   if (wfname != fname) {
       mch_remove(wfname);
       eeglFree(wfname);
   }

   if (end == 0) { // Error encountered.
      if (errmsg == NULL) {
         if (gotInterruptG)
            errmsg = (CS)_(e_interrupted);
         else
            errmsg = (CS)_(e_write_error_file_system_full);
      }

      //If we have a backup file, try to put it in place of the new file, because the new file is 
      //probably corrupt. This avoids losing the original file when trying to make a backup when 
      //writing the file a second time. When "backup_copy" is set we need to copy the backup over 
      //the new file. Otherwise rename the backup file.
      //If this is OK, don't give the extra warning message.
      if (backup) {
         if (backup_copy) {
            //This may take a while, if we were interrupted let the user know we got the message
            if (gotInterruptG) {
               msg(_(e_interrupted));
               out_flush();
            }
            if ((fd = open((char *)backup, O_RDONLY | O_EXTRA, 0)) >= 0) {
               if ((writeInfo.fd 
                     = open((char *)fname,
                      O_WRONLY | O_CREAT | O_TRUNC | O_EXTRA, perm & 0777)
                  ) >= 0
               ) {
                  // copy the file.
                  writeInfo.bw_buf = smallbuf;
                  while ((writeInfo.bw_len = fiReadEintr(fd, smallbuf, SMALLBUFSIZE)) > 0) {
                     if (writeBytes(&writeInfo) == FAIL)
                        break;
                  } 

                  if (close(writeInfo.fd) >= 0 && writeInfo.bw_len == 0)
                     end = 1;      // success
               }
               close(fd);   // ignore errors for closing read file
            }
         } else {
            if (eeRename(backup, fname) == 0)
               end = 1;
         }
      }
      goto fail;
   }

   lnum -= start;     // compute number of written lines
   --no_wait_return;  // may wait for return now

   if (!filtering) {
      msg_add_fname(book, fname);   // put fname in IObuff with quotes
      c = false;
      if (device) {
         STRCAT(IObuff, _("[Device]"));
         c = true;
      } ei (newfile) {
         STRCAT(IObuff, new_file_message());
         c = true;
      }
      if (no_eol) {
         msg_add_eol();
         c = true;
      }
      msg_add_lines(c, (long)lnum, nchars);   // add line/char count
      if (append)
         STRCAT(IObuff, _(" appended"));
      else
         STRCAT(IObuff, _(" written"));

      set_keep_msg(msgTruncDeco(IObuff, 0), 0);
   }

   //When written everything correctly: reset 'modified'.  Unless not
   //writing to the original file and '+' is not in 'cpoptions'.
   if (reset_changed && whole && !append && overwriting) {
      unchanged(book, false);
      //b:changedtick may be incremented in unchanged() but that should not
      //trigger a TextChanged event.
      if (book->lastChangeTick + 1 == CHANGEDTICK(book))
         book->lastChangeTick = CHANGEDTICK(book);
      u_unchanged(book);
      u_update_save_nr(book);
   }

   // If written to the current file, update the timestamp of the swap file
   // and reset the BF_WRITE_MASK flags. Also sets book->modifiedTime.
   if (overwriting) {
      ml_timestamp(book);
      if (append)
         book->flags &= ~BF_NEW;
      else
         book->flags &= ~BF_WRITE_MASK;
   }

   // Remove the backup unless 'backup' option is set
   if (!p_bk && backup && mch_remove(backup) != 0)
      emsg(_(e_cant_delete_backup_file));

   goto nofail;

   // Finish up.  We get here either after failure or success.
fail:
   --no_wait_return;      // may wait for return now
nofail:

   // Done saving, we accept changed book warnings again
   book->isBeingSaved = false;

   eeglFree(backup);
   if (buffer != smallbuf)
      eeglFree(buffer);

   if (errmsg) {
      int numlen = errnum ? (int)STRLEN(errnum) : 0;

      flags = getDecoFlags(HLF_E);   // set highlight for error messages
      msg_add_fname(book, fname);      // put file name in IObuff with quotes
      if (STRLEN(IObuff) + STRLEN(errmsg) + numlen >= IOSIZE)
          IObuff[IOSIZE - STRLEN(errmsg) - numlen - 1] = ZERO;
      // If the error message has the form "is ...", put the error number in
      // front of the file name.
      if (errnum) {
          STRMOVE(IObuff + numlen, IObuff);
          MEMMOVE(IObuff, errnum, (Unt)numlen);
      }
      STRCAT(IObuff, errmsg);
      emsg(IObuff);
      if (errmsg_allocated)
         eeglFree(errmsg);

      retval = FAIL;
      if (end == 0) {
         msgPutsDeco(
             _("\nWARNING: Original file may be lost or damaged\n"), flags | MSG_HIST
         );
         msgPutsDeco(
            _("don't quit the editor until the file is successfully written!"), flags | MSG_HIST
         );

         // Update the timestamp to avoid an "overwrite changed file"
         // prompt when writing again.
         if (stat((char *)fname, &stOld) >= 0) {
            buf_store_time(book, &stOld, fname);
            book->readTime = book->modifiedTime;
            book->readTimeNs = book->modifiedTimeNs;
         }
      }
   }
   msg_scroll = msg_save;

   // When writing the whole file and 'undofile' is set, also write the undo file.
   if (retval == OK && write_undo_file) {
      Byte hash[UNDO_HASH_SIZE];

      sha256_finish(&sha_ctx, hash);
      u_write_undo(NULL, false, book, hash);
   }

   if (!should_abort(retval)) {
   AutocommSave   aco;

   curBook->noEolLnum = 0;  // in case it was set by the previous read

   // Apply POST autocommands.
   // Careful: The autocommands may call bookWrite() recursively!
   // Only do this when a portal was found for "book".
   auCommPrepareBook(&aco, book);
   if (curBook == book) {
       if (append)
      auCommApplyWithInvo(EVENT_FILEAPPENDPOST, fname, fname,
                        false, curBook, invo);
       ei (filtering)
      auCommApplyWithInvo(EVENT_FILTERWRITEPOST, NULL, fname,
                        false, curBook, invo);
       ei (reset_changed && whole)
      auCommApplyWithInvo(EVENT_BUFWRITEPOST, fname, fname,
                        false, curBook, invo);
       else
      auCommApplyWithInvo(EVENT_FILEWRITEPOST, fname, fname,
                        false, curBook, invo);

       // restore curPor/curBook and a few other things
       auCommRestoreBook(&aco);
   }

   if (aborting())       // autocmds may abort script processing
      retval = false;
   }

   // Make sure marks will be written out to the eeglinfo file later, even when
   // the file is new.
   curBook->haveReadEeglinfoMarks = true;

   gotInterruptG |= prev_gotInterruptG;

   return retval;
}

//}}}
//{{{argument lists (lists of open files)

#define AL_SET   1
#define AL_ADD   2
#define AL_DEL   3

// This flag is set whenever the argument list is being changed and calling a
// function that might trigger an autocommand.
private int arglist_locked = false;

private int
check_arglist_locked(void) {
   if (arglist_locked) {
      emsg(_(e_cannot_change_arglist_recursively));
      return FAIL;
   }
   return OK;
}

// Clear an argument list: free all file names and reset it to zero entries.
pub void
alist_clear(EeArgList* al) {
   if (check_arglist_locked() == FAIL)
      return;
   while (--al->al_ga.len >= 0)
      eeglFree(AARGLIST(al)[al->al_ga.len].fname);
   ga_clear(&al->al_ga);
}

// Init an argument list.
pub void
alist_init(EeArgList *al) {
   ga_init2(&al->al_ga, sizeof(ArgFileEntry), 5);
}

// Remove a reference from an argument list.
// Ignored when the argument list is the global one.
// If the argument list is no longer used by any portal, free it.
pub void
alist_unlink(EeArgList *al) {
   if (al != &argListG && --al->al_refcount <= 0) {
      alist_clear(al);
      eeglFree(al);
   }
}

// Create a new argument list and use it for the current portal.
pub void
alist_new(void) {
   curPor->argList = ALLOC_ONE(EeArgList);
   if (curPor->argList == NULL) {
      curPor->argList = &argListG;
      ++argListG.al_refcount;
   } else {
      curPor->argList->al_refcount = 1;
      curPor->argList->id = ++max_alist_id;
      alist_init(curPor->argList);
   }
}


// Set the argument list for the current portal.
// Takes over the allocated files[] and the allocated fnames in it.
private void
alist_set(
   EeArgList* al,
   Boole useCurBook,
   Arr(int) fnum_list,
   Unt fnum_len,
   OUT ExpandMatch* files
){
   if (check_arglist_locked() == FAIL)
      return;

   alist_clear(al);
   if (GA_GROW_OK(&al->al_ga, (int)files->len)) {
      for (Unt i = 0; i < files->len; ++i) {
         if (gotInterruptG) {
            // When adding many books this can take a long time. Allow interrupting here.
            while (i < files->len) {
               eeglFree(files->c[i]);
               i++;
            }
            break;
         }

         // May set book name of a book previously used for the
         // argument list, so that it's re-used by arglistIngest.
         if (fnum_list && i < fnum_len) {
            arglist_locked = true;
            bookSetName(fnum_list[i], files->c[i]);
            arglist_locked = false;
         }

         arglistIngest(al, files->c[i], useCurBook ? 2 : 1);
         ui_breakcheck();
      }
      eeglFree(files);
   }
   if (al == &argListG)
      arg_had_last = false;
}

// Add file "fname" to argument list "al".
// "fname" must have been allocated and "al" must have been checked for room.
// May trigger Buf* autocommands
pub void
arglistIngest(
    EeArgList* al,
    CS fname,
    int set_fnum   // 1: set book number; 2: re-use curBook
){
   if (fname == NULL)      // don't add NULL file names
      return;
   if (check_arglist_locked() == FAIL)
      return;
   arglist_locked = true;
   curPor->locked = true;

   AARGLIST(al)[al->al_ga.len].fname = fname;
   if (set_fnum > 0) {
      AARGLIST(al)[al->al_ga.len].fnum =
          bookOpen(fname, BLN_LISTED | (set_fnum == 2 ? BLN_CURBOOK : 0));
   } 
   ++al->al_ga.len;

   arglist_locked = false;
   curPor->locked = false;
}

//Isolate one argument, taking backticks. Change the argument in-place, puts a ZERO after it.
//Backticks remain. Return a pointer to the start of the next argument.
private CS
do_one_arg(CS str) {
   CS p;
   Boole inbacktick = false;
   for (p = str; *str; ++str) {
      // When the backslash is used for escaping the special meaning of a
      // character, we need to keep it until wildcard expansion.
      if (rem_backslash(str)) {
          *p++ = *str++;
          *p++ = *str;
      } else {
         // An item ends at a space not in backticks
         if (!inbacktick && isSpace(*str))
            break;
         if (*str == '`')
            inbacktick = !inbacktick;
         *p++ = *str;
      }
   }
   str = skipwhite(str);
   *p = ZERO;

   return str;
}

// Separate the arguments in "str" and return a list of pointers in the growarray "gap".
private int
get_arglist(ArrayList *gap, CS str, Boole escaped) {
   ga_init2(gap, sizeof(CS), 20);
   while (*str != ZERO) {
      if (ga_grow(gap, 1) == FAIL) {
         ga_clear(gap);
         return FAIL;
      }
      ((Byte **)gap->c)[gap->len++] = str;

      // If str is escaped, don't handle backslashes or spaces
      if (!escaped)
         return OK;

      // Isolate one argument, change it in-place, put a ZERO after it.
      str = do_one_arg(str);
   }
   return OK;
}

//Parse a list of arguments (file names), expand them and return in "fnames[fcountp]".  
//When "wildignore" is true, removes files matching 'wildignore'. Return FAIL or OK.
pub int
bookParseAndExpandFnames(CS str, Boole omitWildignore, OUT ExpandMatch* matches){
   ArrayList   ga;

   if (get_arglist(&ga, str, false) == FAIL)
      return FAIL;
   int i;
   if (omitWildignore == true)
      i = expand_wildcards(
            ga.len, (Byte **)ga.c, EW_FILE|EW_NOTFOUND|EW_NOTWILD, OUT matches
      );
   else
      i = gen_expand_wildcards(
            ga.len, (Byte **)ga.c, EW_FILE|EW_NOTFOUND|EW_NOTWILD, OUT matches
      );

   ga_clear(&ga);
   return i;
}

// Check the validity of the arg_idx for each other portal.
private void
alist_check_arg_idx(void) {
   Portal   *port;
   Tab   *tp;

   FOR_ALL_TAB_PORTALS(tp, port) {
      if (port->argList == curPor->argList)
         check_arg_idx(port);
   } 
}

//Add files to the arglist of the current portal after arg "after".
//The file names in files[count] must have been allocated and are taken over.
//Files[] itself is not taken over.
private void
alist_add_list(
   ExpandMatch files,
   int after,       // where to add: 0 = before first one
   Boole will_edit  // will edit adding argument
){
   int old_argcount = ARGCOUNT;
   if (check_arglist_locked() != FAIL && GA_GROW_OK(&(curPor->argList)->al_ga, (int)files.len)) {
      if (after < 0)
         after = 0;
      if (after > ARGCOUNT)
         after = ARGCOUNT;
      if (after < ARGCOUNT)
         MEMMOVE(
            ARGLIST + after + files.len, ARGLIST + after, 
            (ARGCOUNT - after) * sizeof(ArgFileEntry)
         );
      arglist_locked = true;
      curPor->locked = true;
      for (Unt i = 0; i < files.len; ++i) {
         int flags = BLN_LISTED | (will_edit ? BLN_CURBOOK : 0);

         ARGLIST[after + i].fname = files.c[i];
         ARGLIST[after + i].fnum = bookOpen(files.c[i], flags);
      }
      arglist_locked = false;
      curPor->locked = false;
      curPor->argList->al_ga.len += files.len;
      if (old_argcount > 0 && curPor->argListInd >= after)
          curPor->argListInd += files.len;
      return;
   }
}

// Delete the file names in 'alist_ga' from the argument list.
private void
arglist_del_files(ArrayList *alist_ga) {
   RegMatch   regmatch;
   int didone;
   int i;
   CS p;
   int match;

   // Delete the items: use each item as a regexp and find a match in the argument list.
   regmatch.rm_ic = false;   // ignore case when 'fileignorecase' is set
   for (i = 0; i < alist_ga->len && !gotInterruptG; ++i) {
      p = ((Byte **)alist_ga->c)[i];
      p = file_pat_to_reg_pat(p, NULL, NULL);
      regmatch.regprog = compileRegexp(p, RE_MAGIC);
      if (regmatch.regprog == NULL) {
         eeglFree(p);
         break;
      }

      didone = false;
      for (match = 0; match < ARGCOUNT; ++match) {
         if (eeRegexec(&regmatch, alist_name(&ARGLIST[match]), (ColNr)0)) {
            didone = true;
            eeglFree(ARGLIST[match].fname);
            MEMMOVE(ARGLIST + match, ARGLIST + match + 1,
               (ARGCOUNT - match - 1) * sizeof(ArgFileEntry));
            --curPor->argList->al_ga.len;
            if (curPor->argListInd > match)
                --curPor->argListInd;
            --match;
         }
      }
      
      eeRegFree(regmatch.regprog);
      eeglFree(p);
      if (!didone)
         showErrFmtMsg(_(e_no_match_str_2), ((Byte **)alist_ga->c)[i]);
   }
   ga_clear(alist_ga);
}

// "what" == AL_SET: Redefine the argument list to 'str'.
// "what" == AL_ADD: add files in 'str' to the argument list after "after".
// "what" == AL_DEL: remove files in 'str' from the argument list.
//
// Return FAIL for failure, OK otherwise.
private int
do_arglist(
   CS str,
   int what,
   int after,   // 0 means before first one
   Boole will_edit   // will edit added argument
){
   ArrayList   new_ga;
   int      i;
   int      arg_escaped = true;

   if (check_arglist_locked() == FAIL)
      return FAIL;

   // Set default argument for ":argadd" command.
   if (what == AL_ADD && *str == ZERO) {
      if (!curBook->fullFileName)
         return FAIL;
      str = curBook->currFileName;
      arg_escaped = false;
   }

   // Collect all file name arguments in "new_ga".
   if (get_arglist(&new_ga, str, arg_escaped) == FAIL)
      return FAIL;

   ExpandMatch files;
   files.a = createArena();
   int retval = FAIL;
   
   if (what == AL_DEL)
      arglist_del_files(&new_ga);
   else {
      i = expand_wildcards(
            new_ga.len, (Byte **)new_ga.c, EW_DIR|EW_FILE|EW_ADDSLASH|EW_NOTFOUND, OUT &files
      );
      ga_clear(&new_ga);
      if (i == FAIL || files.len == 0) {
         emsg(_(e_no_match));
         goto cleanup;
      }

      if (what == AL_ADD) {
         alist_add_list(files, after, will_edit);
      } else // what == AL_SET
         alist_set(curPor->argList, will_edit, NULL, 0, OUT &files);
   }

   alist_check_arg_idx();
   retval = OK;
   
cleanup:
   deleteArena(files.a);
   return retval;
}

// Redefine the argument list.
pub void
set_arglist(CS str) {
    do_arglist(str, AL_SET, 0, true);
}

// Return true if portal "port" is editing the file at the current argument index.
pub int
editing_arg_idx(Portal *port) {
    return !(port->argListInd >= WARGCOUNT(port)
      || (port->book->fiNum != WARGLIST(port)[port->argListInd].fnum
          && (port->book->fullFileName == NULL
             || !(fullpathcmp(
                alist_name(&WARGLIST(port)[port->argListInd]),
           port->book->fullFileName, true, true) & FPC_SAME))));
}

// Check if portal "port" is editing the argListInd file in its argument list.
pub void
check_arg_idx(Portal* port) {
   if (WARGCOUNT(port) > 1 && !editing_arg_idx(port)) {
      // We are not editing the current entry in the argument list.
      // Set "arg_had_last" if we are editing the last one.
      port->isNotValid = true;
      if (port->argListInd != WARGCOUNT(port) - 1
         && arg_had_last == false
         && port->argList == &argListG
         && GARGCOUNT > 0
         && port->argListInd < GARGCOUNT
         && (port->book->fiNum == GARGLIST[GARGCOUNT - 1].fnum
             || (port->book->fullFileName != NULL
            && (fullpathcmp(alist_name(&GARGLIST[GARGCOUNT - 1]),
              port->book->fullFileName, true, true) & FPC_SAME)))
      )
          arg_had_last = true;
   } else {
      // We are editing the current entry in the argument list.
      // Set "arg_had_last" if it's also the last one
      port->isNotValid = false;
      if (port->argListInd == WARGCOUNT(port) - 1 && port->argList == &argListG)
          arg_had_last = true;
   }
}

// ":args", ":arglocal" and ":argglobal".
pub void
c_args(Invocation* invo) {
   int      i;

   if (invo->id != C_args) {
      if (check_arglist_locked() == FAIL)
         return;
      alist_unlink(curPor->argList);
      if (invo->id == C_argglobal)
         curPor->argList = &argListG;
      else // invo->id == C_arglocal
         alist_new();
    }

   // ":args file ..": define new argument list, handle like ":next"
   // Also for ":argslocal file .." and ":argsglobal file ..".
   if (*invo->arg != ZERO) {
      if (check_arglist_locked() == FAIL)
         return;
      c_next(invo);
      return;
   }

   // ":args": list arguments.
   if (invo->id == C_args) {
      if (ARGCOUNT <= 0)
         return;      // empty argument list

      Arr(CS) items = ALLOC_MULT(CS, ARGCOUNT);

      // Overwrite the command, for a short list there is no scrolling
      // required and no wait_return().
      gotoCommline(true);

      for (i = 0; i < ARGCOUNT; ++i)
         items[i] = alist_name(&ARGLIST[i]);
      listInColumns(items, ARGCOUNT, curPor->argListInd, false);
      eeglFree(items);

      return;
   }

   // ":argslocal": make a local copy of the global argument list.
   if (invo->id == C_arglocal) {
      ArrayList   *gap = &curPor->argList->al_ga;

      if (GA_GROW_FAILS(gap, GARGCOUNT))
         return;

      for (i = 0; i < GARGCOUNT; ++i) {
         if (GARGLIST[i].fname != NULL) {
            AARGLIST(curPor->argList)[gap->len].fname =
                copyStr(GARGLIST[i].fname);
            AARGLIST(curPor->argList)[gap->len].fnum =
                GARGLIST[i].fnum;
            ++gap->len;
         }
      } 
   }
}

// ":previous", ":sprevious", ":Next" and ":sNext".
pub void
c_previous(Invocation* invo){
   // If past the last one already, go to the last one.
   if (curPor->argListInd - (int)invo->line2 >= ARGCOUNT)
      do_argfile(invo, ARGCOUNT - 1);
   else
      do_argfile(invo, curPor->argListInd - (int)invo->line2);
}

// ":rewind", ":first", ":sfirst" and ":srewind".
pub void
c_rewind(Invocation* invo){
   do_argfile(invo, 0);
}

// ":last" and ":slast".
pub void
c_last(Invocation* invo){
   do_argfile(invo, ARGCOUNT - 1);
}

// ":argument" and ":sargument".
pub void
c_argument(Invocation* invo){
   int      i;
   if (invo->addr_count > 0)
      i = invo->line2 - 1;
   else
      i = curPor->argListInd;
   do_argfile(invo, i);
}

// Edit file "argn" of the argument lists.
pub void
do_argfile(Invocation* invo, int argn){
   CS p;
   int      old_arg_idx = curPor->argListInd;
   Boole isSplitCommand = *invo->comm == 's';

   if (portErrorIfPopup(true))
      return;
   if (argn < 0 || argn >= ARGCOUNT) {
      if (ARGCOUNT <= 1)
         emsg(_(e_there_is_only_one_file_to_edit));
      ei (argn < 0)
         emsg(_(e_cannot_go_before_first_file));
      else
         emsg(_(e_cannot_go_beyond_last_file));

      return;
   }

   if (!isSplitCommand
          && (&ARGLIST[argn])->fnum != curBook->fiNum
          && !portCheckCanSetCurBookForceIt(invo->forceit))
      return;

   setpcmark();

   // split portal or create new tab first
   if (isSplitCommand || commModifierG.cmod_tab != 0) {
      if (splitPortal(0, 0) == FAIL)
         return;
      curPor->o.diff = false;
   } else {
      // if 'hidden' set, only check for changed file when re-editing the same book
      Boole sameFile = false;
      p = fiExpandAndCopy(alist_name(&ARGLIST[argn]), true);
      sameFile = fNameMatchesCurBook(p);
      eeglFree(p);
      if (sameFile
         && check_changed(curBook, CCGD_AW
             | (sameFile ? CCGD_MULTWIN : 0)
             | (invo->forceit ? CCGD_FORCEIT : 0)
             | CCGD_EXCMD)
      )
          return;
   }

   curPor->argListInd = argn;
   if (argn == ARGCOUNT - 1 && curPor->argList == &argListG)
      arg_had_last = true;

   // Edit the file; always use the last known line number.
   // When it fails (e.g. Abort for already edited file) restore the argument index.
   if (startEditingFile(0, alist_name(&ARGLIST[curPor->argListInd]), NULL,
         invo, ECMD_LAST,
         (ECMD_HIDE) + (invo->forceit ? ECMD_FORCEIT : 0), curPor) == FAIL
   )
      curPor->argListInd = old_arg_idx;
   // like Vi: set the mark where the cursor is in the file.
   ei (invo->id != C_argdo)
      setmark('\'');
}

// ":next", and commands that behave like it.
pub void
c_next(Invocation* invo){
   // check for changed book now, if this fails the argument list is not redefined.
   int i;
   if (*invo->arg != ZERO) {    // redefine file list
      if (do_arglist(invo->arg, AL_SET, 0, true) == FAIL)
         return;
      i = 0;
   } else
      i = curPor->argListInd + (int)invo->line2;
   do_argfile(invo, i);
}

// ":argdedupe"
pub void
c_argdedupe(Invocation*){
   for (int i = 0; i < ARGCOUNT; ++i) {
      // Expand each argument to a full path to catch different paths leading to the same file
      CS firstFullname = fiExpandAndCopy(ARGLIST[i].fname, false);
      if (!firstFullname)
          return;

      for (int j = i + 1; j < ARGCOUNT; ++j) {
         CS secondFullname = fiExpandAndCopy(ARGLIST[j].fname, false);
         if (secondFullname == NULL)
            break;  // out of memory
         int areNamesDuplicate = fnamecmp(firstFullname, secondFullname) == 0;
         eeglFree(secondFullname);

         if (areNamesDuplicate) {
            // remove one duplicate argument
            eeglFree(ARGLIST[j].fname);
            MEMMOVE(ARGLIST + j, ARGLIST + j + 1, (ARGCOUNT - j - 1) * sizeof(ArgFileEntry));
            --ARGCOUNT;

            if (curPor->argListInd == j)
                curPor->argListInd = i;
            ei (curPor->argListInd > j)
                --curPor->argListInd;

            --j;
         }
      }

      eeglFree(firstFullname);
   }
}

pub void
c_argedit(Invocation* invo) {
   int i = invo->addr_count ? (int)invo->line2 : curPor->argListInd + 1;
   // Whether curBook will be reused, curBook->fullFileName will be set.
   Boole isReusable = isCurBookReusable();

   if (do_arglist(invo->arg, AL_ADD, i, true) == FAIL)
      return;

   if (curPor->argListInd == 0
          && (curBook->mem.flags & ML_EMPTY)
          && (curBook->fullFileName == NULL || isReusable))
      i = 0;
   // Edit the argument.
   if (i < ARGCOUNT)
      do_argfile(invo, i);
}

pub void
c_argadd(Invocation* invo) {
   do_arglist(invo->arg, AL_ADD,
          invo->addr_count > 0 ? (int)invo->line2 : curPor->argListInd + 1,
          false);
}

pub void
c_argdelete(Invocation* invo) {
   int      i;
   int      n;

   if (check_arglist_locked() == FAIL)
      return;

   if (invo->addr_count > 0 || *invo->arg == ZERO) {
   // ":argdel" works like ":.argdel"
   if (invo->addr_count == 0) {
       if (curPor->argListInd >= ARGCOUNT) {
      emsg(_(e_no_argument_to_delete));
      return;
       }
       invo->line1 = invo->line2 = curPor->argListInd + 1;
   } ei (invo->line2 > ARGCOUNT)
       // ":1,4argdel": Delete all arguments in the range.
       invo->line2 = ARGCOUNT;
   n = invo->line2 - invo->line1 + 1;
   if (*invo->arg != ZERO)
       // Can't have both a range and an argument.
       emsg(_(e_invalid_argument));
   ei (n <= 0) {
       // Don't give an error for ":%argdel" if the list is empty.
       if (invo->line1 != 1 || invo->line2 != 0)
      emsg(_(e_invalid_range));
   } else {
       for (i = invo->line1; i <= invo->line2; ++i)
      eeglFree(ARGLIST[i - 1].fname);
       MEMMOVE(ARGLIST + invo->line1 - 1, ARGLIST + invo->line2,
         (Unt)((ARGCOUNT - invo->line2) * sizeof(ArgFileEntry)));
       curPor->argList->al_ga.len -= n;
       if (curPor->argListInd >= invo->line2)
      curPor->argListInd -= n;
       ei (curPor->argListInd > invo->line1)
      curPor->argListInd = invo->line1;
       if (ARGCOUNT == 0)
      curPor->argListInd = 0;
       ei (curPor->argListInd >= ARGCOUNT)
      curPor->argListInd = ARGCOUNT - 1;
   }
   } else
      do_arglist(invo->arg, AL_DEL, 0, false);
}

// Function given to expandGeneric() to obtain the possible arguments of the argedit and argdelete 
// commands.
pub CS
get_arglist_name(Expand*, int idx) {
   return (idx >= ARGCOUNT) ? S"" : alist_name(&ARGLIST[idx]);
}

// Get the file name for an argument list entry.
pub CS
alist_name(ArgFileEntry *afe) {
   // Use the name from the associated book if it exists.
   Book* b = bookFindFileByBookNr(afe->fnum);
   if (!b || b->currFileName == NULL)
      return afe->fname;
   return b->currFileName;
}

// Close all the portals containing files which are not in the argument list.
// Used by the ":all" command.
private void
argAllCloseUnusedPortals(ArgAllState *aall) {
   Portal* po;
   Portal* wpnext;
   Tab* tNext;
   Book* book;
   int i;
   Portal* old_curPor;
   Tab* old_curtab;

   old_curPor = curPor;
   old_curtab = curtab;

   if (aall->had_tab > 0)
      gotoTab(firstTabG, true, true);

   // moving tabs around in an autocommand may cause an endless loop
   movingTabsForbiddenG++;
   for (;;) {
      tNext = curtab->next;
      for (po = firstPor; po != NULL; po = wpnext) {
         wpnext = po->next;
         book = po->book;
         if (!book->fullFileName
                || (!aall->keep_tabs && (book->countPortals > 1 || po->width != visibleColsG))
         )
            i = aall->opened_len;
         else {
            // check if the book in this portal is in the arglist
            for (i = 0; i < aall->opened_len; ++i) {
               if (i < aall->alist->al_ga.len
                   && (AARGLIST(aall->alist)[i].fnum == book->fiNum
                     || fullpathcmp(alist_name( &AARGLIST(aall->alist)[i]),
                        book->fullFileName, true, true) & FPC_SAME)
               ) {
               int weight = 1;

               if (old_curtab == curtab) {
                  ++weight;
                  if (old_curPor == po)
                     ++weight;
               }

               if (weight > (int)aall->opened[i]) {
                  aall->opened[i] = (Byte)weight;
                  if (i == 0) {
                     if (aall->new_curPor != NULL)
                        aall->new_curPor->argListInd = aall->opened_len;
                     aall->new_curPor = po;
                     aall->new_curtab = curtab;
                  }
               } ei (aall->keep_tabs)
                  i = aall->opened_len;

               if (po->argList != aall->alist) {
                   // Use the current argument list for all portals
                   // containing a file from it.
                   alist_unlink(po->argList);
                   po->argList = aall->alist;
                   ++po->argList->al_refcount;
               }
               break;
               }
            }
         }
         po->argListInd = i;

         if (i == aall->opened_len && !aall->keep_tabs) {// close this portal
            // If the book was changed, and we would like to hide it, try autowriting.
            // don't close last portal
            if (ONLY_ONE_PORTAL && (firstTabG->next == NULL || !aall->had_tab))
               aall->use_firstPor = true;
            else {
               closePortal(po, false);

               // check if autocommands removed the next portal
               if (!portalIsValid(wpnext))
                  wpnext = firstPor;   // start all over...
            }
         }
      }

      // Without the ":tab" modifier only do the current tab.
      if (aall->had_tab == 0 || tNext == NULL)
         break;

      // check if autocommands removed the next tab
      if (!isTabValid(tNext))
         tNext = firstTabG;   // start all over...

      gotoTab(tNext, true, true);
   }
   movingTabsForbiddenG--;
}

// Open upto "count" portals for the files in the argument list 'aall->alist'.
private void
openPortalsIntoFiles(ArgAllState *aall, int count) {
   Portal   *po;
   Boole tabDropEmptyPortal = false;
   int      i;
   int      split_ret = OK;
   int      p_ea_save;

   // ":tab drop file" should re-use an empty portal to avoid "--remote-tab"
   // leaving an empty tab when executed locally.
   if (aall->keep_tabs && CURBOOK_EMPTY() && curBook->countPortals == 1
             && curBook->fullFileName == NULL && !curBook->wasModified
   ) {
      aall->use_firstPor = true;
      tabDropEmptyPortal = true;
   }

   for (i = 0; i < count && !gotInterruptG; ++i) {
      if (aall->alist == &argListG && i == argListG.al_ga.len - 1)
         arg_had_last = true;
      if (aall->opened[i] > 0) {
         // Move the already present portal to below the current portal
         if (curPor->argListInd != i) {
            FOR_ALL_PORTALS(po) {
               if (po->argListInd == i) {
                  if (aall->keep_tabs) {
                      aall->new_curPor = po;
                      aall->new_curtab = curtab;
                  } ei (po->frame->parent   != curPor->frame->parent) {
                      emsg(_(e_portal_layout_changed_unexpectedly));
                      i = count;
                      break;
                  } else
                     portMoveAfter(po, curPor);
                  break;
               }
            }
         }
      } ei (split_ret == OK) {
         // trigger events for tab drop
         if (tabDropEmptyPortal && i == count - 1)
            --autocmd_no_enter;
         if (!aall->use_firstPor) { // split current portal
            p_ea_save = p_ea;
            p_ea = true;      // use space from all portals
            split_ret = splitPortal(0, WSP_ROOM | WSP_BELOW);
            p_ea = p_ea_save;
            if (split_ret == FAIL)
               continue;
         } else    // first portal: do autocomm for leaving this book
            --autocmd_no_leave;

         // edit file "i"
         curPor->argListInd = i;
         if (i == 0) {
            aall->new_curPor = curPor;
            aall->new_curtab = curtab;
         }
         (void)startEditingFile(
            0, alist_name(&AARGLIST(aall->alist)[i]), NULL, NULL, ECMD_ONE, ECMD_HIDE + ECMD_OLDBUF,
            curPor
         );
         if (tabDropEmptyPortal && i == count - 1)
            ++autocmd_no_enter;
         if (aall->use_firstPor)
            ++autocmd_no_leave;
         aall->use_firstPor = false;
      }
      ui_breakcheck();

      // When ":tab" was used open a new tab for a new portal repeatedly.
      if (aall->had_tab > 0)
         commModifierG.cmod_tab = 9999;
   }
}

// openAllArgs(): Open up to "count" portals, one for each argument.
private void
openAllArgs(
    int   count,
    int   forceit,      // hide books in current portals
    int keep_tabs      // keep current tabs, for ":tab drop file"
){
    ArgAllState   aall;
    Portal      *last_curPor;
    Tab      *last_curtab;
    int         prev_arglist_locked = arglist_locked;

   if (commPortTypeG != 0) {
      emsg(_(e_invalid_in_commline_portal));
      return;
   }
   if (ARGCOUNT <= 0) {
      // Don't give an error message. We don't want it when the ":all" command is in the .vimrc.
      return;
   }
   setpcmark();

   aall.use_firstPor = false;
   aall.had_tab = commModifierG.cmod_tab;
   aall.new_curPor = NULL;
   aall.new_curtab = NULL;
   aall.forceit = forceit;
   aall.keep_tabs = keep_tabs;
   aall.opened_len = ARGCOUNT;
   aall.opened = allocZeroed(aall.opened_len);

   // Autocommands may do anything to the argument list.  Make sure it's not
   // freed while we are working here by "locking" it.  We still have to
   // watch out for its size being changed.
   aall.alist = curPor->argList;
   ++aall.alist->al_refcount;
   arglist_locked = true;

   Tab *new_lu_tp = curtab;

   // Stop Visual mode, the cursor and "VIsual" may very well be invalid after
   // switching to another book.
   reset_VIsual_and_resel();

   //Try closing all portals that are not in the argument list. Also close portals that are not 
   //full width; When 'hidden' or "forceit" set the book becomes hidden. Portals that have a 
   //changed book and can't be hidden won't be closed. When the ":tab" modifier was used do 
   //this for all tabs.
   argAllCloseUnusedPortals(&aall);

   // Open a portal into files in the argument list that don't have one.
   // ARGCOUNT may change while doing this, because of autocommands.
   if (count > aall.opened_len || count <= 0)
      count = aall.opened_len;

   // Don't execute Win/Buf Enter/Leave autocommands here.
   ++autocmd_no_enter;
   ++autocmd_no_leave;
   last_curPor = curPor;
   last_curtab = curtab;
   enterPortal(lastPor, false);

   // Open up to "count" portals.
   openPortalsIntoFiles(&aall, count);

   // Remove the "lock" on the argument list.
   alist_unlink(aall.alist);
   arglist_locked = prev_arglist_locked;

   --autocmd_no_enter;

   // restore last referenced tab's curPor
   if (last_curtab != aall.new_curtab) {
      if (isTabValid(last_curtab))
          gotoTab(last_curtab, true, true);
      if (portalIsValid(last_curPor))
          enterPortal(last_curPor, false);
   }
   // to portal with first arg
   if (isTabValid(aall.new_curtab))
      gotoTab(aall.new_curtab, true, true);

   // Now set the last used tabpage to where we started.
   if (isTabValid(new_lu_tp))
      lastUsedTabG = new_lu_tp;

   if (portalIsValid(aall.new_curPor))
      enterPortal(aall.new_curPor, false);

   --autocmd_no_leave;
   eeglFree(aall.opened);
}

// ":all" and ":sall". Also used for ":tab drop file ..." after setting the argument list.
pub void
c_all(Invocation* invo) {
   if (invo->addr_count == 0)
      invo->line2 = 9999;
   openAllArgs((int)invo->line2, invo->forceit, invo->id == C_drop);
}

//Concatenate all files in the argument list, separated by spaces, and return it in one allocated 
//string. Spaces and backslashes in the file names are escaped with a backslash.
//Return NULL when out of memory.
pub CS
arg_all(void) {
   int      len;
   int      idx;
   CS retval = NULL;
   CS p;

   // Do this loop twice: first time: compute the total length second time: concatenate the names
   for (;;) {
      len = 0;
      for (idx = 0; idx < ARGCOUNT; ++idx) {
         p = alist_name(&ARGLIST[idx]);
         if (p == NULL)
            continue;
         if (len > 0) {
            // insert a space in between names
            if (retval != NULL)
                retval[len] = ' ';
            ++len;
         }
         for ( ; *p != ZERO; ++p) {
            if (*p == ' '
               || *p == '\\'
               || *p == '`'
            ) {
               // insert a backslash
               if (retval != NULL)
                  retval[len] = '\\';
               ++len;
            }
            if (retval != NULL)
                retval[len] = *p;
            ++len;
         }
      }

      // second time: break here
      if (retval != NULL) {
         retval[len] = ZERO;
         break;
      }

      // allocate memory
      retval = alloc(len + 1);
   }

   return retval;
}

// "argc([portal id])" function
pub void
f_argc(Var* argvars, Var* returnVar) {
   Portal* po;

   if (argvars[0].tag == VAR_UNKNOWN)
      // use the current portal
      returnVar->number = ARGCOUNT;
   ei (argvars[0].tag == VAR_NUMBER && tv_get_number(&argvars[0]) == -1)
      // use the global argument list
      returnVar->number = GARGCOUNT;
   else {
      // use the argument list of the specified portal
      po = portFindByNrOrId(&argvars[0]);
      if (po != NULL)
         returnVar->number = WARGCOUNT(po);
      else
         returnVar->number = -1;
   }
}

pub void
f_argidx(Var*, OUT Var* returnVar) {
   returnVar->number = curPor->argListInd;
}

pub void
f_arglistid(Var *argvars, OUT Var* returnVar) {
   Portal   *po;

   returnVar->number = -1;
   po = find_tabwin(&argvars[0], &argvars[1], NULL);
   if (po != NULL)
      returnVar->number = po->argList->id;
}

// Get the argument list for a given portal
private void
get_arglist_as_returnVar(ArgFileEntry *arglist, Unt argcount, OUT Var* returnVar) {
   allocReturnList(returnVar);
   if (arglist) {
      for (Unt idx = 0; idx < argcount; ++idx)
         list_append_string(returnVar->list, alist_name(&arglist[idx]), -1);
   }
}

// "argv(nr)" function
pub void
f_argv(Var *argvars, OUT Var* returnVar) {
   ArgFileEntry   *arglist = NULL;
   Unt argcount = 0;

   if (argvars[0].tag == VAR_UNKNOWN) {
      get_arglist_as_returnVar(ARGLIST, ARGCOUNT, returnVar);
      return;
   }

   if (argvars[1].tag == VAR_UNKNOWN) {
      arglist = ARGLIST;
      argcount = ARGCOUNT;
   } ei (argvars[1].tag == VAR_NUMBER && tv_get_number(&argvars[1]) == -1) {
      arglist = GARGLIST;
      argcount = GARGCOUNT;
   } else {
      Portal* po = portFindByNrOrId(&argvars[1]);
      if (po) {
         // Use the argument list of the specified portal
         arglist = WARGLIST(po);
         argcount = WARGCOUNT(po);
      }
   }

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
   int idx = varGetNumberChk(&argvars[0], NULL);
   if (arglist && idx >= 0 && idx < (int)argcount)
      returnVar->string = copyStr(alist_name(&arglist[idx]));
   ei (idx == -1)
      get_arglist_as_returnVar(arglist, argcount, returnVar);
}

//}}}
//{{{text properties. See ":help text-properties".

//In a hashtable item "hi_key" points to "name" in a PropType.
//This avoids adding a pointer to the hashtable item.
//PT2HIKEY() converts a proptype pointer to a hashitem key pointer.
//HIKEY2PT() converts a hashitem key pointer to a proptype pointer.
//HI2PT() converts a hashitem pointer to a proptype pointer.
#define PT2HIKEY(p)  (mbText((p)->name))
#define HIKEY2PT(p)   ((PropType *)((p) - offsetof(PropType, name)))
#define HI2PT(hi)      HIKEY2PT((hi)->hi_key)

// The global text property types.
// property types for compilation errors and warning, used for example after :make
//private PropType compilationTypesS[] = {
//   (PropType) {.id = 0, .ty = 0, .hilite = 123, .priority = 0, .flags = PT_FLAG_COMBINE},
//   (PropType) {.id = 1, .ty = 0, .hilite = 123, .priority = 0, .flags = PT_FLAG_COMBINE}
//};
private EeSet *global_proptypes = NULL;
private PropType **global_proparray = NULL;

// The last used text property type ID.
private int proptype_id = 0;

// Find a property type by name, return the hashitem.
// Returns NULL if the item can't be found.
private EeSetItem *
findPropTypeHash(Text name, Book* book) {
   if (name.len == 0)
      return NULL;
      
   EeSet* ht = book ? book->propTypes : global_proptypes;
   if (!ht)
      return NULL;
      
   EeSetItem* hi = hash_find(ht, name);
   if (HASHITEM_EMPTY(hi))
      return NULL;
   return hi;
}

// Like findPropTypeHash() but return the property type.
private PropType *
findPropTypeByName(Text name, Book* book) {
   EeSetItem* hi = findPropTypeHash(name, book);
   return hi ?  HI2PT(hi) : null;
}

// Get the prop type ID of "name". When not found return zero.
pub int
findPropTypeIdByName(Text name, Book* book) {
   PropType *pt = findPropTypeByName(name, book);

   if (pt == NULL)
      return 0;
   return pt->id;
}

// Lookup a property type by name.  First in "book" and when not found in the global types.
// When not found gives an error message and returns NULL.
private PropType *
lookup_prop_type(Text name, Book* book) {
   PropType *type = findPropTypeByName(name, book);

   if (type == NULL)
      type = findPropTypeByName(name, NULL);
   if (type == NULL)
      showErrFmtMsg(_(e_property_type_str_does_not_exist), name);
   return type;
}

//Get an optional "bufnr" item from the dict in "arg".
//When the argument is not used or "bufnr" is not present then "book" is unchanged.
//If "bufnr" is valid or not present return OK.
//When "arg" is not a dict or "bufnr" is invalid return FAIL.
private Unt
getBookNrFromArg(Var *arg, Book** book) {
   if (arg->tag != VAR_BAG) {
      emsg(_(e_dictionary_required));
      return FAIL;
   }
   if (!arg->bag)
      return OK;  // NULL dict is like an empty dict
      
   DictItem* di = bagFind(arg->bag, tConst("bufnr"));
   if (di && (di->c.tag != VAR_NUMBER || di->c.number != 0)) {
      *book = evGetBookArg(&di->c);
      if (*book == NULL)
          return FAIL;
   }
   return OK;
}

//prop_add({lnum}, {col}, {props})
pub void
f_prop_add(Var *argvars, OUT Var* returnVar) {
   LineNr start_lnum = tv_get_number(&argvars[0]);
   ColNr start_col = tv_get_number(&argvars[1]);
   if (check_for_dict_arg(argvars, 2) == FAIL)
      return;

   returnVar->number = prop_add_common(start_lnum, start_col,
             argvars[2].bag, curBook, &argvars[2]);
}

//Attach a text property 'type_name' to the text starting at [start_lnum, start_col] and ending at
//[end_lnum, end_col] in the book "book" and assign identifier "id".
//When "text" is not NULL add it to book->textPropText[-id - 1].
private int
addProp(OUT Book* book, Prop prop) {
   LineNr   lnum;
   int      proplen;
   CS newprops;
   Unt textlen;
   CS newtext;
   int i;
   TextProp tmpProp;
   NULLABLE CS text = prop.text;
   int res = FAIL;

   PropType* type = lookup_prop_type(mbText(prop.tyName), book);
   if (!type)
      goto theend;

   if (prop.startLnum < 1 || prop.startLnum > book->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), (long)prop.startLnum);
      goto theend;
   }
   if (prop.endLnum < prop.startLnum || prop.endLnum > book->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), (long)prop.endLnum);
      goto theend;
   }

   if (book->mem.mfile == NULL) {
      emsg(_(e_cannot_add_text_property_to_unloaded_buffer));
      goto theend;
   }

   if (prop.text) {
      ArrayList* gap = &book->textPropText;
      CS p;

      // double check we got the right ID
      if (-prop.id - 1 != gap->len)
         internalErrMsg(S"text prop ID mismatch");
      if (gap->ga_growsize == 0)
         ga_init2(gap, sizeof(char *), 50);
      if (ga_grow(gap, 1) == FAIL)
         goto theend;
      ((Byte **)gap->c)[gap->len] = text;
      gap->len++; 

      // change any control character (Tab, Newline, etc.) to a Space to make
      // it simpler to compute the size
      for (p = prop.text; *p != ZERO; MB_PTR_ADV(p)) {
         if (*p < ' ')
            *p = ' ';
      } 
      text = NULL;
   }

   for (lnum = prop.startLnum; lnum <= prop.endLnum; ++lnum) {
      ColNr sort_col;   // column where it appears
      long   length;       // in bytes

      // Fetch the line to get the lineLen field updated.
      CS props;
      proplen = get_text_props(OUT &props, book, lnum, true);
      textlen = book->mem.lineLen - proplen * sizeof(TextProp);

      ColNr col;       // start column use in col
      if (lnum == prop.startLnum)
         col = prop.startCol;
      else
         col = 1;
      if (col - 1 > (ColNr)textlen && !(col == 0 && prop.text != NULL)) {
         showErrFmtMsg(_(e_invalid_column_number_nr), (long)prop.startCol);
         goto theend;
      }
      sort_col = col;

      if (lnum == prop.endLnum)
         length = prop.endCol - col;
      else
         length = (int)textlen - col + 1;
      if (length > (long)textlen)
         length = (int)textlen;   // can include the end-of-line
      if (length < 0)
         length = 0;      // zero-width property

      if (prop.text != NULL) {
         length = 1;      // text is placed on one character
         if (col == 0) {
            col = MAXCOL;   // before or after the line
            if ((prop.textFlags & TEXT_PROP_ALIGN_ABOVE) == 0)
               sort_col = MAXCOL;
            length += prop.textPaddingLeft;
         }
      }

      // Allocate the new line with space for the new property.
      newtext = alloc(book->mem.lineLen + sizeof(TextProp));
      // Copy the text, including terminating ZERO.
      MEMMOVE(newtext, book->mem.cachedLine, textlen);

      // Find the index where to insert the new property.
      // Since the text properties are not aligned properly when stored with
      // the text, we need to copy them as bytes before using it as a struct.
      for (i = 0; i < proplen; ++i) {
         MEMMOVE(&tmpProp, props + i * sizeof(TextProp), sizeof(TextProp));
         // col is MAXCOL when the text goes above or after the line, when
         // above we should use column zero for sorting
         ColNr propCol = (tmpProp.flags & TEXT_PROP_ALIGN_ABOVE) ? 0 : tmpProp.col;
         if (propCol >= sort_col)
            break;
      }
      newprops = newtext + textlen;
      if (i > 0)
         MEMMOVE(newprops, props, sizeof(TextProp) * i);

      tmpProp.col = col;
      tmpProp.len = length;
      tmpProp.id = prop.id;
      tmpProp.type = type->id;
      tmpProp.flags = prop.textFlags
                | (lnum > prop.startLnum ? TEXT_PROP_CONT_PREV : 0)
                | (lnum < prop.endLnum ? TEXT_PROP_CONT_NEXT : 0)
                | ((type->flags & PT_FLAG_INS_START_INCL) ? TEXT_PROP_START_INCL : 0);
      tmpProp.leftPad = prop.textPaddingLeft;
      MEMMOVE(newprops + i * sizeof(TextProp), &tmpProp, sizeof(TextProp));

      if (i < proplen) {
         MEMMOVE(
            newprops + (i + 1) * sizeof(TextProp), props + i * sizeof(TextProp), 
            sizeof(TextProp) * (proplen - i)
         );
      } 

      if ((book->mem.flags & ML_LINE_DIRTY) != 0)
         eeglFree(book->mem.cachedLine);
      book->mem.cachedLine = newtext;
      book->mem.lineLen += sizeof(TextProp);
      book->mem.flags |= ML_LINE_DIRTY;
   }

   normInvalidateDisplayOfChangedBookLine(book);
   doChangedLinesBook(book, prop.startLnum, prop.endLnum + 1, 0);
   res = OK;

theend:
   eeglFree(text);
   return res;
}

//prop_add_list()
//First argument specifies the text property:
//  {'type': <str>, 'id': <num>, 'bufnr': <num>}
//Second argument is a List where each item is a List with the following
//entries: [lnum, start_col, end_col]
pub void
f_prop_add_list(Var *argvars, OUT Var*) {
   Book* book = curBook;
   int id = 0;
   ListItem* li;
   Boole error = false;
   int prev_anyEmsgG = anyEmsgG;
   Prop prop;

   if (check_for_dict_arg(argvars, 0) == FAIL || confirmVarIsList(argvars, 1) == FAIL)
      return;

   if (confirmVarIsNonnullList(argvars, 1) == FAIL)
      return;

   Bag* b = argvars[0].bag;
   if (!b || !bagHasKey(b, tConst("type"))) {
      emsg(_(e_missing_property_type_name));
      return;
   }

   if (bagHasKey(b, tConst("id"))) {
      Long x;
      x = bagGetNumber(b, tConst("id"));
      if (x > INT_MAX || x  <= INT_MIN) {
         showErrFmtMsg(_(e_val_too_large), bagGetString(b, tConst("id"), false));
         return;
      }
      id = (int)x;
   }

   if (getBookNrFromArg(&argvars[0], &book) == FAIL)
      return;

   // This must be done _before_ we start adding properties because property changes trigger book
   // (memline) reorganisation, which needs this flag to be correctly set.
   book->hasTextprop = true;  // this is never reset
   FOR_ALL_LIST_ITEMS(argvars[1].list, li) {
      if (li->c.tag != VAR_LIST || li->c.list == NULL) {
         emsg(_(e_list_required));
         return;
      }

      List* posList = li->c.list;
      prop.startLnum = list_find_nr(posList, 0L, OUT &error);
      if (!error)
         prop.startCol = list_find_nr(posList, 1L, OUT &error);
      if (!error)
         prop.endLnum = list_find_nr(posList, 2L, OUT &error);
      if (!error)
         prop.endCol = list_find_nr(posList, 3L, OUT &error);
      prop.id = id;
      if (!error && posList->len > 4)
          prop.id = list_find_nr(posList, 4L, OUT &error);
      if (error || prop.startLnum <= 0 || prop.startCol <= 0 
            || prop.endLnum <= 0 || prop.endCol <= 0
      ) {
         if (prev_anyEmsgG == anyEmsgG)
            emsg(_(e_invalid_argument));
         return;
      }
      if (addProp(OUT book, prop))
//            type_name, thisId, NULL, 0, 0, start_lnum, end_lnum, start_col, end_col) == FAIL)
         return;
   }

   drawBookLater(book, UPD_VALID);
}

// Get the next ID to use for a textprop with text in book.
private int
get_textprop_id(Book* book) {
   // TODO: recycle deleted entries
   return -(book->textPropText.len + 1);
}

// Flag that is set when a negative ID isused for a normal text property.
// It is then impossible to use virtual text properties.
private int didUseNegativePropIdS = false;

//Shared between prop_add() and createPopup().
//"dict_arg" is the function argument of a dict containing "bufnr".
//it is NULL for createPopup(). Return the "id" used for "text" or zero.
pub int
prop_add_common(
   LineNr startLnum,
   ColNr startCol,
   Bag* dict,
   Book* defaultBook,
   Var* dictArg
){
   Book* book = defaultBook;
   int      id = 0;
   CS text = NULL;

   if (!dict || !bagHasKey(dict, tConst("type"))) {
      emsg(_(e_missing_property_type_name));
      goto theend;
   }
   Prop prop = {};
   prop.startLnum = startLnum;
   prop.startCol = startCol;

   if (bagHasKey(dict, tConst("end_lnum"))) {
      prop.endLnum = bagGetNumber(dict, tConst("end_lnum"));
      if (prop.endLnum < startLnum) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), S"end_lnum");
         goto theend;
      }
   } else
      prop.endLnum = startLnum;

   if (bagHasKey(dict, tConst("length"))) {
      long length = bagGetNumber(dict, tConst("length"));

      if (length < 0 || prop.endLnum > prop.startLnum) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), "length");
         goto theend;
      }
      prop.endCol = prop.startCol + length;
   } ei (bagHasKey(dict, tConst("end_col"))) {
      prop.endCol = bagGetNumber(dict, tConst("end_col"));
      if (prop.endCol <= 0) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), "end_col");
         goto theend;
      }
   } ei (prop.startLnum == prop.endLnum)
      prop.endCol = prop.startCol;
   else
      prop.endCol = 1;

   if (bagHasKey(dict, tConst("id"))) {
      Long x;
      x = bagGetNumber(dict, tConst("id"));
      if (x > INT_MAX || x  <= INT_MIN) {
         showErrFmtMsg(_(e_val_too_large), bagGetString(dict, tConst("id"), false));
         goto theend;
      }
      prop.id = (int)x;
   }

   if (bagHasKey(dict, tConst("text"))) {
      if (bagHasKey(dict, tConst("length")) 
            || bagHasKey(dict, tConst("end_col")) 
            || bagHasKey(dict, tConst("end_lnum"))
      ){
         emsg(_(e_cannot_use_length_endcol_and_endlnum_with_text));
         goto theend;
      }

      prop.text = bagGetString(dict, tConst("text"), true);
      if (prop.text == NULL)
         goto theend;
      // use a default length of 1 to make multiple props show up
      prop.endCol = startCol + 1;

      if (bagHasKey(dict, tConst("text_align"))) {
         CS p = bagGetString(dict, tConst("text_align"), false);
         if (!p)
            goto theend;
            
         if (prop.startCol != 0) {
            emsg(_(e_can_only_use_text_align_when_column_is_zero));
            goto theend;
         }
         if (STRCMP(p, "right") == 0)
            prop.textFlags |= TEXT_PROP_ALIGN_RIGHT;
         ei (STRCMP(p, "above") == 0)
            prop.textFlags |= TEXT_PROP_ALIGN_ABOVE;
         ei (STRCMP(p, "below") == 0)
            prop.textFlags |= TEXT_PROP_ALIGN_BELOW;
         ei (STRCMP(p, "after") != 0) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "text_align", p);
            goto theend;
         }
      }

      if (bagHasKey(dict, tConst("text_padding_left"))) {
         int textPaddingLeft = bagGetNumber(dict, tConst("text_padding_left"));
         if (textPaddingLeft < 0) {
            showErrFmtMsg(_(e_argument_must_be_positive_str), "text_padding_left");
            goto theend;
         }
      }

      if (bagHasKey(dict, tConst("text_wrap"))) {
         CS p = bagGetString(dict, tConst("text_wrap"), false);
         if (!p)
            goto theend;
         if (STRCMP(p, "wrap") == 0)
            prop.textFlags |= TEXT_PROP_WRAP;
         ei (STRCMP(p, "truncate") != 0) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "text_wrap", p);
            goto theend;
         }
      }
   }

   // Column must be 1 or more for a normal text property; when "text" is
   // present zero means it goes after the line.
   if (startCol < (text == NULL ? 1 : 0)) {
      showErrFmtMsg(_(e_invalid_column_number_nr), (long)startCol);
      goto theend;
   }
   if (startCol > 0 && prop.textPaddingLeft > 0) {
      emsg(_(e_can_only_use_left_padding_when_column_is_zero));
      goto theend;
   }

   if (dictArg && getBookNrFromArg(dictArg, &book) == FAIL)
      goto theend;

   if (id < 0) {
      if (book->textPropText.len > 0) {
         emsg(_(e_cannot_use_negative_id_after_adding_textprop_with_text));
         goto theend;
      }
      didUseNegativePropIdS = true;
   }

   if (text) {
      if (didUseNegativePropIdS) {
         emsg(_(e_cannot_add_textprop_with_text_after_using_textprop_with_negative_id));
         goto theend;
      }
      id = get_textprop_id(book);
   }

   // This must be done _before_ we add the property because property changes
   // trigger book (memline) reorganization, which needs this flag to be correctly set.
   book->hasTextprop = true;  // this is never reset

   addProp(OUT book, prop);
   text = NULL;

   drawBookLater(book, UPD_VALID);

theend:
   eeglFree(text);
   return id;
}

//Fetch the text properties for line "lnum" in book "book".
//Return the number of text properties and, when non-zero, a pointer to the
//first one in "props" (note that it is not aligned, therefore the Byte pointer).
pub int
get_text_props(OUT CS* props, Book* book, LineNr lnum, Boole will_change) {
   // Be quick when no text property types have been defined for the book,
   // unless we are adding one.
   if ((!book->hasTextprop && !will_change) || book->mem.mfile == NULL)
      return 0;

   // Fetch the line to get the lineLen field updated.
   CS text = memGetLine(book, lnum, will_change);
   Unt textlen = memGetBookLen(book, lnum) + 1;
   Unt proplen = book->mem.lineLen - textlen;
   if (proplen == 0)
      return 0;
   if (proplen % sizeof(TextProp) != 0) {
      internalErrMsg(e_text_property_info_corrupted);
      return 0;
   }
   *props = text + textlen;
   return (int)(proplen / sizeof(TextProp));
}

//Return the number of text properties with "above" or "below" alignment in
//line "lnum".  A "right" aligned property also goes below after a "below" or
//other "right" aligned property.
pub int
prop_count_above_below(Book* book, LineNr lnum) {
   CS props;
   int count = get_text_props(OUT &props, book, lnum, false);
   int result = 0;
   TextProp   prop;
   int      i;
   int      next_right_goes_below = false;

   if (count == 0)
      return 0;
   for (i = 0; i < count; ++i) {
      MEMMOVE(&prop, props + i * sizeof(prop), sizeof(prop));
      if (prop.col == MAXCOL && text_prop_type_valid(book, &prop)) {
         if ((prop.flags & (TEXT_PROP_ALIGN_ABOVE | TEXT_PROP_ALIGN_BELOW))
             || (next_right_goes_below && (prop.flags & TEXT_PROP_ALIGN_RIGHT))
         ){
            ++result;
         } ei (prop.flags & TEXT_PROP_ALIGN_RIGHT)
            next_right_goes_below = true;
      }
   }
   return result;
}

//Return the number of text properties on line "lnum" in the current book.
//When "only_starting" is true only text properties starting in this line will be considered.
//When "last_line" is false then text properties after the line are not counted.
pub int
count_props(LineNr lnum, int only_starting, int last_line) {
   CS props;
   int      proplen = get_text_props(OUT &props, curBook, lnum, false);
   int      result = proplen;
   int      i;
   TextProp   prop;

   for (i = 0; i < proplen; ++i) {
      MEMMOVE(&prop, props + i * sizeof(prop), sizeof(prop));
      // A prop is dropped when in the first line and it continues from the
      // previous line, or when not in the last line and it is virtual text after the line.
      if ((only_starting && (prop.flags & TEXT_PROP_CONT_PREV))
         || (!last_line && prop.col == MAXCOL))
          --result;
   }
   return result;
}

private TextProp   *text_prop_compare_props;
private Book      *text_prop_compare_buf;

//Score for sorting on position of the text property: 0: above,
//1: after (default), 2: right, 3: below (comes last)
private int
text_prop_order(int flags) {
   if (flags & TEXT_PROP_ALIGN_ABOVE)
      return 0;
   if (flags & TEXT_PROP_ALIGN_RIGHT)
      return 2;
   if (flags & TEXT_PROP_ALIGN_BELOW)
      return 3;
   return 1;
}

//Function passed to qsort() to sort text properties.
//Return 1 if "s1" has priority over "s2", -1 if the other way around, zero if
//both have the same priority.
private int
text_prop_compare(const void *s0, const void *s1) {
   int  idx0, idx1;
   TextProp   *tp0, *tp1;
   PropType  *pt0, *pt1;
   ColNr col0, col1;

   idx0 = *(int *)s0;
   idx1 = *(int *)s1;
   tp0 = &text_prop_compare_props[idx0];
   tp1 = &text_prop_compare_props[idx1];
   col0 = tp0->col;
   col1 = tp1->col;

   // property that inserts text has priority over one that doesn't
   if ((tp0->id < 0) != (tp1->id < 0))
      return tp0->id < 0 ? 1 : -1;

   if (col0 == MAXCOL || col1 == MAXCOL) {
   int order0 = text_prop_order(tp0->flags);
   int order1 = text_prop_order(tp1->flags);

   if (order0 != order1)
       return order0 < order1 ? 1 : -1;
   }

   // check highest priority, defined by the type
   pt0 = text_prop_type_by_id(text_prop_compare_buf, tp1->type);
   pt1 = text_prop_type_by_id(text_prop_compare_buf, tp1->type);
   if (pt0 != pt1) {
      if (pt0 == NULL)
          return -1;
      if (pt1 == NULL)
          return 1;
      if (pt0->priority != pt1->priority)
          return pt0->priority > pt1->priority ? 1 : -1;
    }

   // same priority, one that starts first wins
   if (col0 != col1)
      return col0 < col1 ? 1 : -1;

   // for a property with text the id can be used as tie breaker
   if (tp0->id < 0)
      return tp0->id > tp1->id ? 1 : -1;

   return 0;
}

//Sort "count" text properties using an array if indexes "idxs" into the list
//of text props "props" for book "book".
pub void
sort_text_props(
   Book* book,
   TextProp* props,
   int* idxs,
   int count
) {
   text_prop_compare_buf = book;
   text_prop_compare_props = props;
   qsort((void *)idxs, (Unt)count, sizeof(int), text_prop_compare);
}

//Find text property "type_id" in the visible lines of portal "wp".
//Match "id" when it is > 0. Return FAIL when not found.
pub int
find_visible_prop(
   Portal       *wp,
   int       type_id,
   int       id,
   TextProp  *prop,
   LineNr    *found_lnum
) {
   // return when "type_id" no longer exists
   if (text_prop_type_by_id(wp->book, type_id) == NULL)
      return FAIL;

   // bottomLine may not have been updated yet.
   validate_botline_win(wp);
   for (LineNr lnum = wp->topLine; lnum < wp->bottomLine; ++lnum) {
      CS props;
      int   count = get_text_props(OUT &props, wp->book, lnum, false);
      for (int i = 0; i < count; ++i) {
         MEMMOVE(prop, props + i * sizeof(TextProp), sizeof(TextProp));
         if (prop->type == type_id && (id <= 0 || prop->id == id)) {
            *found_lnum = lnum;
            return OK;
         }
      }
   }
   return FAIL;
}

//Set the text properties for line "lnum" to "props" with length "len".
//If "len" is zero text properties are removed, "props" is not used.
//Any existing text properties are dropped. Only work for the current book.
private void
set_text_props(LineNr lnum, CS props, int len) {
   CS text = ml_get(lnum);
   int textlen = ml_get_len(lnum) + 1;
   CS newtext = alloc(textlen + len);
   MEMMOVE(newtext, text, textlen);
   if (len > 0)
      MEMMOVE(newtext + textlen, props, len);
   if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
      eeglFree(curBook->mem.cachedLine);
   curBook->mem.cachedLine = newtext;
   curBook->mem.lineLen = textlen + len;
   curBook->mem.flags |= ML_LINE_DIRTY;
}

//Add "text_props" with "text_prop_count" text properties to line "lnum".
pub void
add_text_props(LineNr lnum, TextProp *text_props, int text_prop_count) {
   int       proplen = text_prop_count * (int)sizeof(TextProp);

   CS text = ml_get(lnum);
   CS newtext = alloc(curBook->mem.lineLen + proplen);
   MEMMOVE(newtext, text, curBook->mem.lineLen);
   MEMMOVE(newtext + curBook->mem.lineLen, text_props, proplen);
   if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
      eeglFree(curBook->mem.cachedLine);
   curBook->mem.cachedLine = newtext;
   curBook->mem.lineLen += proplen;
   curBook->mem.flags |= ML_LINE_DIRTY;
}

//Function passed to qsort() for sorting PropType on id.
private int
compare_pt(void const * s0, void const* s1) {
   PropType* tp0 = *(PropType **)s0;
   PropType* tp1 = *(PropType **)s1;

   return tp0->id == tp1->id ? 0 : tp0->id < tp1->id ? -1 : 1;
}

private PropType*
find_type_by_id(EeSet* ht, PropType*** array, int id) {
   if (!ht || ht->count == 0)
      return NULL;

   int low = 0;
   int high;
   // Make the lookup faster by creating an array with pointers to
   // hashtable entries, sorted on id.
   if (*array == NULL) {
      EeSetItem  *hi;
      int       i = 0;

      *array = ALLOC_MULT(PropType *, ht->count);
      long todo = (long)ht->count;
      FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
         if (!HASHITEM_EMPTY(hi)) {
            (*array)[i++] = HI2PT(hi);
            --todo;
         }
      }
      qsort((void *)*array, ht->count, sizeof(PropType *), compare_pt);
   }

   // binary search in the sorted array
   high = ht->count;
   while (high > low) {
      int m = (high + low) / 2;

      if ((*array)[m]->id == id)
         return (*array)[m];
      if ((*array)[m]->id > id)
         high = m;
      else
         low = m + 1;
    }
    return NULL;
}

// Fill 'dict' with text properties in 'prop'.
private void
prop_fill_dict(Bag* dict, TextProp* prop, Book* book) {
   PropType *pt;
   int buflocal = true;
   int virtualtext_prop = prop->id < 0;

   bagAddNumber(dict, S"col", (prop->col == MAXCOL) ? 0 : prop->col);
   if (!virtualtext_prop) {
      bagAddNumber(dict, S"length", prop->len);
      bagAddNumber(dict, S"id", prop->id);
   }
   bagAddNumber(dict, S"start", !(prop->flags & TEXT_PROP_CONT_PREV));
   bagAddNumber(dict, S"end", !(prop->flags & TEXT_PROP_CONT_NEXT));

   pt = find_type_by_id(book->propTypes, &book->propArray, prop->type);
   if (!pt) {
      pt = find_type_by_id(global_proptypes, &global_proparray, prop->type);
      buflocal = false;
   }
   if (pt)
      bagAddString(dict, S"type", pt->name);

   if (buflocal)
      bagAddNumber(dict, S"type_bufnr", book->fiNum);
   else
      bagAddNumber(dict, S"type_bufnr", 0);
   if (virtualtext_prop) {
      // virtual text property
      ArrayList    *gap = &book->textPropText;

      // negate the property id to get the string index
      CS text = ((Byte **)gap->c)[-prop->id - 1];
      bagAddString(dict, S"text", text);

      // text_align
      CS text_align = NULL;
      if (prop->flags & TEXT_PROP_ALIGN_RIGHT)
          text_align = S"right";
      ei (prop->flags & TEXT_PROP_ALIGN_ABOVE)
          text_align = S"above";
      ei (prop->flags & TEXT_PROP_ALIGN_BELOW)
          text_align = S"below";
      if (text_align != NULL)
          bagAddString(dict, S"text_align", text_align);

      // text_wrap
      if (prop->flags & TEXT_PROP_WRAP)
          bagAddString(dict, S"text_wrap", S"wrap");
      if (prop->leftPad != 0)
          bagAddNumber(dict, S"text_padding_left", prop->leftPad);
   }
}

// Find a property type by ID in "book" or globally. Returns NULL if not found.
pub PropType *
text_prop_type_by_id(Book* book, int id) {
   PropType* ty = find_type_by_id(book->propTypes, &book->propArray, id);
   if (!ty)
      ty = find_type_by_id(global_proptypes, &global_proparray, id);
   return ty;
}

// Return true if "prop" is a valid text property type.
private int
text_prop_type_valid(Book* book, TextProp *prop) {
   return text_prop_type_by_id(book, prop->type) != NULL;
}

//prop_clear({lnum} [, {lnum_end} [, {bufnr}]])
pub void
f_prop_clear(Var *argvars, OUT Var*) {
   Book* book = curBook;
   int did_clear = false;

   LineNr start = tv_get_number(&argvars[0]);
   LineNr end = start;
   if (argvars[1].tag != VAR_UNKNOWN) {
      end = tv_get_number(&argvars[1]);
      if (argvars[2].tag != VAR_UNKNOWN && getBookNrFromArg(&argvars[2], &book) == FAIL)
         return;
   }
   if (start < 1 || end < 1) {
      emsg(_(e_invalid_range));
      return;
   }

   for (LineNr lnum = start; lnum <= end; ++lnum) {
      if (lnum > book->mem.lineCount)
         break;
      CS text = memGetLine(book, lnum, false);
      Unt len = memGetBookLen(book, lnum) + 1;
      if ((Unt)book->mem.lineLen > len) {
         did_clear = true;
         if (!(book->mem.flags & ML_LINE_DIRTY)) {
            CS newtext = copyStr(text);

            book->mem.cachedLine = newtext;
            book->mem.flags |= ML_LINE_DIRTY;
         }
         book->mem.lineLen = (int)len;
      }
   }
   if (did_clear)
      drawBookLater(book, UPD_NOT_VALID);
}

//prop_find({props} [, {direction}])
pub void
f_prop_find(Var *argvars, OUT Var* returnVar) {
   Pos       *cursor = &curPor->cursor;
   Book       *book = curBook;
   int      start_pos_has_prop = 0;
   int      seen_end = false;
   int      id = 0;
   int      id_found = false;
   int      type_id = -1;
   int      lnum = -1;
   int      col = -1;
   Unt      dir = FORWARD;    // FORWARD == 1, BACKWARD == -1
   int      both;

   if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
      return;
   Bag* b = argvars[0].bag;

   if (getBookNrFromArg(&argvars[0], &book) == FAIL)
      return;
   if (book->mem.mfile == NULL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN) {
      CS dir_s = tv_get_string(&argvars[1]);

      if (*dir_s == 'b')
         dir = BACKWARD;
      ei (*dir_s != 'f') {
         emsg(_(e_invalid_argument));
         return;
      }
   }

   DictItem* di = bagFind(b, tConst("lnum"));
   if (di)
      lnum = tv_get_number(&di->c);

   di = bagFind(b, tConst("col"));
   if (di != NULL)
      col = tv_get_number(&di->c);

   if (lnum == -1) {
      lnum = cursor->lnum;
      col = cursor->col + 1;
   } ei (col == -1)
      col = 1;

   if (lnum < 1 || lnum > book->mem.lineCount) {
      emsg(_(e_invalid_range));
      return;
   }

   Boole skipstart = bagGetBool(b, tConst("skipstart"), false);

   if (bagHasKey(b, tConst("id"))) {
      id = bagGetNumber(b, tConst("id"));
      id_found = true;
   }
   if (bagHasKey(b, tConst("type"))) {
      CS name = bagGetString(b, tConst("type"), false);
      PropType* type = lookup_prop_type(mbText(name), book);

      if (type == NULL)
         return;
      type_id = type->id;
   }
   both = bagGetBool(b, tConst("both"), false);
   if (!id_found && type_id == -1) {
      emsg(_(e_need_at_least_one_of_id_or_type));
      return;
   }
   if (both && (!id_found || type_id == -1)) {
      emsg(_(e_need_id_and_type_or_types_with_both));
      return;
   }

   int lnum_start = lnum;

   allocReturnDict(returnVar);

   while (1) {
      CS text = memGetLine(book, lnum, false);
      Unt   textlen = memGetBookLen(book, lnum) + 1;
      int   count = (int)((book->mem.lineLen - textlen) / sizeof(TextProp));
      int       i;
      TextProp  prop;
      int       prop_start;
      int       prop_end;

      for (i = dir == BACKWARD ? count - 1 : 0; i >= 0 && i < count; i += dir) {
          MEMMOVE(&prop, text + textlen + i * sizeof(TextProp),
                           sizeof(TextProp));

         // For the very first line try to find the first property before or
         // after `col`, depending on the search direction.
         if (lnum == lnum_start) {
            if (dir == BACKWARD) {
               if (prop.col > col)
                  continue;
            } ei (prop.col + prop.len - (prop.len != 0) < col)
               continue;
         }
         if (both ? prop.id == id && prop.type == type_id
              : (id_found && prop.id == id) || prop.type == type_id
         ){
         // Check if the starting position has text props.
         if (lnum_start == lnum
               && col >= prop.col
               && (col <= prop.col + prop.len - (prop.len != 0))
         )
             start_pos_has_prop = 1;

         // The property was not continued from last line, it starts on
         // this line.
         prop_start = !(prop.flags & TEXT_PROP_CONT_PREV);
         // The property does not continue on the next line, it ends on
         // this line.
         prop_end = !(prop.flags & TEXT_PROP_CONT_NEXT);
         if (!prop_start && prop_end && dir == FORWARD)
             seen_end = 1;

         // Skip lines without the start flag.
         if (!prop_start) {
            // Always search backwards for start when search started
            // on a prop and we're not skipping.
            if (start_pos_has_prop && !skipstart)
               dir = BACKWARD;
            continue;
         }

         // If skipstart is true, skip the prop at start pos (even if
         // continued from another line).
         if (start_pos_has_prop && skipstart && !seen_end) {
             start_pos_has_prop = 0;
             continue;
         }

         prop_fill_dict(returnVar->bag, &prop, book);
         bagAddNumber(returnVar->bag, S"lnum", lnum);

         return;
          }
      }

      if (dir > 0) {
         if (lnum >= book->mem.lineCount)
            break;
         lnum++;
      } else {
         if (lnum <= 1)
            break;
         lnum--;
      }
   }
}

//Return true if 'type_or_id' is in the 'types_or_ids' list.
private int
prop_type_or_id_in_list(int *types_or_ids, int len, int type_or_id) {
   for (int i = 0; i < len; i++) {
      if (types_or_ids[i] == type_or_id)
          return true;
   } 
   return false;
}

//Return all the text properties in line 'lnum' in book 'book' in 'retlist'.
//If 'prop_types' is not NULL, then return only the text properties with
//matching property type in the 'prop_types' array.
//If 'prop_ids' is not NULL, then return only the text properties with
//an identifier in the 'props_ids' array.
//If 'add_lnum' is true, then add the line number also to the text property dictionary.
private void
get_props_in_line(
   Book* book,
   LineNr lnum,
   int* prop_types,
   int prop_types_len,
   int* prop_ids,
   int prop_ids_len,
   List* retlist,
   int add_lnum
) {
   CS text = memGetLine(book, lnum, false);
   Unt textlen = memGetBookLen(book, lnum) + 1;
   TextProp prop;

   int count = (int)((book->mem.lineLen - textlen) / sizeof(TextProp));
   for (int i = 0; i < count; ++i) {
      MEMMOVE(&prop, text + textlen + i * sizeof(TextProp),
         sizeof(TextProp));
      if ((prop_types == NULL
             || prop_type_or_id_in_list(prop_types, prop_types_len, prop.type))
         && (prop_ids == NULL || prop_type_or_id_in_list(prop_ids, prop_ids_len, prop.id))
      ){
         Bag *b = allocBag();
         prop_fill_dict(b, &prop, book);
         if (add_lnum)
            bagAddNumber(b, S"lnum", lnum);
         listAppendBag(retlist, b);
      }
   }
}

//Convert a List of property type names into an array of property type
//identifiers. Returns a pointer to the allocated array. Returns NULL on
//error. 'num_types' is set to the number of returned property types.
private int *
get_prop_types_from_names(List *l, Book* book, OUT int *num_types) {
   *num_types = 0;

   Arr(int) prop_types = ALLOC_MULT(int, list_len(l));

   int i = 0;
   ListItem   *li;
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag != VAR_STRING) {
          emsg(_(e_string_required));
          goto errret;
      }
      CS name = li->c.string;
      if (!name)
          goto errret;

      PropType* type = lookup_prop_type(text(name), book);
      if (!type)
          goto errret;
      prop_types[i++] = type->id;
   }

   *num_types = i;
   return prop_types;

errret:
   EE_CLEAR(prop_types);
   return NULL;
}

//Convert a List of property identifiers into an array of property
//identifiers.  Returns a pointer to the allocated array. Returns NULL on
//error. 'num_ids' is set to the number of returned property identifiers.
private int*
get_prop_ids_from_list(List *l, OUT int* countIds) {
   ListItem   *li;
   int      i = 0;
   int      id;
   Boole error = false;

   *countIds = 0;

   Arr(int) prop_ids = ALLOC_MULT(int, list_len(l));

   CHECK_LIST_MATERIALIZE(l);
   FOR_ALL_LIST_ITEMS(l, li) {
      error = false;
      id = varGetNumberChk(&li->c, OUT &error);
      if (error)
         goto errret;

      prop_ids[i++] = id;
   }

   *countIds = i;
   return prop_ids;

errret:
   EE_CLEAR(prop_ids);
   return NULL;
}

//prop_list({lnum} [, {bufnr}])
pub void
f_prop_list(Var *argvars, OUT Var* returnVar) {
   LineNr   lnum;
   Book* book = curBook;
   int      add_lnum = false;
   int      *prop_types = NULL;
   int      prop_types_len = 0;
   int      *prop_ids = NULL;
   int      prop_ids_len = 0;
   List   *l;
   DictItem   *di;

   allocReturnList(returnVar);

   // default: get text properties on current line
   LineNr start_lnum = tv_get_number(&argvars[0]);
   LineNr end_lnum = start_lnum;
   if (argvars[1].tag != VAR_UNKNOWN) {

      if (check_for_dict_arg(argvars, 1) == FAIL)
         return;
      Bag* d = argvars[1].bag;

      if (getBookNrFromArg(&argvars[1], &book) == FAIL)
         return;

      if (d && (di = bagFind(d, tConst("end_lnum"))) != NULL) {
         if (di->c.tag != VAR_NUMBER) {
            emsg(_(e_number_required));
            return;
         }
         end_lnum = tv_get_number(&di->c);
         if (end_lnum < 0)
            // negative end_lnum is used as an offset from the last book line
            end_lnum = book->mem.lineCount + end_lnum + 1;
         ei (end_lnum > book->mem.lineCount)
            end_lnum = book->mem.lineCount;
         add_lnum = true;
      }
      if (d != NULL && (di = bagFind(d, tConst("types"))) != NULL) {
         if (di->c.tag != VAR_LIST) {
            emsg(_(e_list_required));
            return;
         }

         l = di->c.list;
         if (l != NULL && list_len(l) > 0) {
            prop_types = get_prop_types_from_names(l, book, &prop_types_len);
            if (prop_types == NULL)
                return;
         }
      }
      if (d != NULL && (di = bagFind(d, tConst("ids"))) != NULL) {
         if (di->c.tag != VAR_LIST) {
            emsg(_(e_list_required));
            goto errret;
         }

         l = di->c.list;
         if (l && list_len(l) > 0) {
            prop_ids = get_prop_ids_from_list(l, OUT &prop_ids_len);
            if (!prop_ids)
               goto errret;
         }
      }
   }
   if (start_lnum < 1 || start_lnum > book->mem.lineCount
      || end_lnum < 1 || end_lnum < start_lnum)
      emsg(_(e_invalid_range));
   else {
      for (lnum = start_lnum; lnum <= end_lnum; lnum++)
          get_props_in_line(book, lnum, prop_types, prop_types_len,
             prop_ids, prop_ids_len,
             returnVar->list, add_lnum);
   } 

errret:
   EE_CLEAR(prop_types);
   EE_CLEAR(prop_ids);
}

// prop_remove({props} [, {lnum} [, {lnum_end}]])
pub void
f_prop_remove(Var *argvars, OUT Var* returnVar) {
   LineNr   start = 1;
   LineNr   end = 0;
   LineNr   lnum;
   LineNr   first_changed = 0;
   LineNr   last_changed = 0;
   Book   *book = curBook;
   int      do_all;
   int      id = -MAXCOL;
   int      type_id = -1;       // for a single "type"
   int      *typeIds = NULL;   // array, for a list of "types", allocated
   int      num_typeIds = 0;   // number of elements in "typeIds"
   int      both;
   int      did_remove_text = false;

   returnVar->number = 0;

   if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN) {
      start = tv_get_number(&argvars[1]);
      end = start;
      if (argvars[2].tag != VAR_UNKNOWN)
          end = tv_get_number(&argvars[2]);
      if (start < 1 || end < 1) {
         emsg(_(e_invalid_range));
         return;
      }
   }

   Bag* dict = argvars[0].bag;
   if (getBookNrFromArg(&argvars[0], &book) == FAIL || book->mem.mfile == NULL)
      return;

   do_all = bagGetBool(dict, tConst("all"), false);

   if (bagHasKey(dict, tConst("id")))
      id = bagGetNumber(dict, tConst("id"));

   // if a specific type was supplied "type": check that (and ignore "types".
   // Otherwise check against the list of "types".
   if (bagHasKey(dict, tConst("type"))) {
      CS name = bagGetString(dict, tConst("type"), false);
      PropType  *type = lookup_prop_type(mbText(name), book);

      if (!type)
         return;
      type_id = type->id;
   }
   if (bagHasKey(dict, tConst("types"))) {
      Var types;
      ListItem *li = NULL;

      bagGetVar(dict, tConst("types"), &types);
      if (types.tag == VAR_LIST && types.list->len > 0) {
         typeIds = alloc( sizeof(int) * types.list->len );
         FOR_ALL_LIST_ITEMS(types.list, li) {
            PropType *prop_type;

            if (li->c.tag != VAR_STRING)
              continue;

            prop_type = lookup_prop_type(mbText(li->c.string), book);

            if (!prop_type)
               goto cleanup_prop_remove;

            typeIds[num_typeIds++] = prop_type->id;
         }
      }
   }
   both = bagGetBool(dict, tConst("both"), false);

   if (id == -MAXCOL && (type_id == -1 && num_typeIds == 0)) {
      emsg(_(e_need_at_least_one_of_id_or_type));
      goto cleanup_prop_remove;
   }
   if (both && (id == -MAXCOL || (type_id == -1 && num_typeIds == 0))) {
      emsg(_(e_need_id_and_type_or_types_with_both));
      goto cleanup_prop_remove;
   }
   if (type_id != -1 && num_typeIds > 0) {
      emsg(_(e_cannot_specify_both_type_and_types));
      goto cleanup_prop_remove;
   }

   if (end == 0)
      end = book->mem.lineCount;
   for (lnum = start; lnum <= end; ++lnum) {
      Unt len;

      if (lnum > book->mem.lineCount)
         break;
      len = memGetBookLen(book, lnum) + 1;
      if ((Unt)book->mem.lineLen > len) {
         static TextProp   textprop;  // static because of alignment
         unsigned      idx;

         for (idx = 0; idx < (book->mem.lineLen - len) / sizeof(TextProp); ++idx) {
            CS cur_prop = book->mem.cachedLine + len + idx * sizeof(TextProp);
            Unt   taillen;
            int matches_id = 0;
            int matchty = 0;

            MEMMOVE(&textprop, cur_prop, sizeof(TextProp));

            matches_id = textprop.id == id;
            if (num_typeIds > 0) {
               for (int idx2 = 0; !matchty && idx2 < num_typeIds; ++idx2)
                  matchty = textprop.type == typeIds[idx2];
            } else {
               matchty = textprop.type == type_id;
            }

            if (both ? matches_id && matchty : matches_id || matchty) {
               if (!(book->mem.flags & ML_LINE_DIRTY)) {
                  // need to allocate the line to be able to change it
                  CS newptr = alloc(book->mem.lineLen);
                  MEMMOVE(newptr, book->mem.cachedLine, book->mem.lineLen);
                  book->mem.cachedLine = newptr;
                  book->mem.flags |= ML_LINE_DIRTY;

                  cur_prop = book->mem.cachedLine + len + idx * sizeof(TextProp);
               }

               taillen = book->mem.lineLen - len - (idx + 1) * sizeof(TextProp);
               if (taillen > 0)
                  MEMMOVE(cur_prop, cur_prop + sizeof(TextProp), taillen);
               book->mem.lineLen -= sizeof(TextProp);
               --idx;

               if (textprop.id < 0) {
                  ArrayList    *gap = &book->textPropText;
                  int       ii = -textprop.id - 1;

                  // negative ID: property with text - free the text
                  if (ii < gap->len) {
                     Byte **p = ((Byte **)gap->c) + ii;
                     EE_CLEAR(*p);
                     did_remove_text = true;
                  }
               }

               if (first_changed == 0)
                  first_changed = lnum;
               last_changed = lnum;
               ++returnVar->number;
               if (!do_all)
                  break;
            }
         }
      }
   }

   if (first_changed > 0) {
      normInvalidateDisplayOfChangedBookLine(book);
      doChangedLinesBook(book, first_changed, last_changed + 1, 0);
      drawBookLater(book, UPD_VALID);
   }

   if (did_remove_text) {
      ArrayList* lst = &book->textPropText;

      // Reduce the arraylist size for NULL pointers at the end.
      while (lst->len > 0 && ((Byte **)lst->c)[lst->len - 1] == NULL)
          --lst->len;
   }

cleanup_prop_remove:
    eeglFree(typeIds);
}

//Common for f_prop_type_add() and f_prop_type_change().
private void
prop_type_set(Var *argvars, int add) {
   CS name;
   Book   *book = NULL;
   DictItem  *di;
   name = tv_get_string(&argvars[0]);
   if (*name == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), "\"\"");
      return;
   }

   if (getBookNrFromArg(&argvars[1], &book) == FAIL)
      return;
   Bag* dict = argvars[1].bag;

   PropType* prop = findPropTypeByName(text(name), book);
   if (add) {
      EeSet **htp;

      if (prop) {
         showErrFmtMsg(_(e_property_type_str_already_defined), name);
         return;
      }
      prop = allocZeroed(offsetof(PropType, name) + STRLEN(name) + 1);
      STRCPY(prop->name, name);
      prop->id = ++proptype_id;
      prop->flags = PT_FLAG_COMBINE;
      if (book == NULL) {
         htp = &global_proptypes;
         EE_CLEAR(global_proparray);
      } else {
         htp = &book->propTypes;
         EE_CLEAR(book->propArray);
      }
      if (*htp == NULL) {
         *htp = ALLOC_ONE(EeSet);
         hash_init(*htp);
      }
      hash_add(*htp, PT2HIKEY(prop), S"prop type");
   } else {
      if (prop == NULL) {
         showErrFmtMsg(_(e_property_type_str_does_not_exist), name);
         return;
      }
   }

   if (dict) {
      di = bagFind(dict, tConst("highlight"));
      if (di) {
         Short hiId = SHORT;

         CS hiliteName = bagGetString(dict, tConst("highlight"), false);
         if (hiliteName && *hiliteName != ZERO)
            hiId = hiliteGroupByName(mbText(hiliteName));
         if (hiId == SHORT) {
            showErrFmtMsg(_(e_unknown_highlight_group_name_str), hiliteName ? hiliteName : S"");
            return;
         }
         prop->hilite = hiId;
      }

      di = bagFind(dict, tConst("combine"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_COMBINE;
         else
            prop->flags &= ~PT_FLAG_COMBINE;
      }

      di = bagFind(dict, tConst("override"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_OVERRIDE;
         else
            prop->flags &= ~PT_FLAG_OVERRIDE;
      }

      di = bagFind(dict, tConst("priority"));
      if (di)
         prop->priority = tv_get_number(&di->c);

      di = bagFind(dict, tConst("start_incl"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_INS_START_INCL;
         else
            prop->flags &= ~PT_FLAG_INS_START_INCL;
      }

      di = bagFind(dict, tConst("end_incl"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_INS_END_INCL;
         else
            prop->flags &= ~PT_FLAG_INS_END_INCL;
      }
   }
}

//prop_type_add({name}, {props})
pub void
f_prop_type_add(Var *argvars, OUT Var*) {
   prop_type_set(argvars, true);
}

//prop_type_change({name}, {props})
pub void
f_prop_type_change(Var *argvars, OUT Var*) {
   prop_type_set(argvars, false);
}

//prop_type_delete({name} [, {bufnr}])
pub void
f_prop_type_delete(Var *argvars, OUT Var*) {
   Book   *book = NULL;

   CS name = tv_get_string(&argvars[0]);
   if (*name == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), "\"\"");
      return;
   }

   if (argvars[1].tag != VAR_UNKNOWN && getBookNrFromArg(&argvars[1], &book) == FAIL)
      return;

   EeSetItem* hi = findPropTypeHash(text(name), book);
   if (!hi)
      return;

   EeSet* ht;
   PropType   *prop = HI2PT(hi);

   if (!book) {
      ht = global_proptypes;
      EE_CLEAR(global_proparray);
   } else {
      ht = book->propTypes;
      EE_CLEAR(book->propArray);
   }
   hash_remove(ht, hi, S"prop type delete");
   eeglFree(prop);

   // currently visible text properties will disappear
   redraw_all_later(UPD_CLEAR);
   didChangePortalSettingBuf(book ? book : curBook);
}

//prop_type_get({name} [, {props}])
pub void
f_prop_type_get(Var *argvars, OUT Var* returnVar) {
   CS name = tv_get_string(&argvars[0]);
   if (*name == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), "\"\"");
      return;
   }

   allocReturnDict(returnVar);

   Book* book = NULL;
   if (argvars[1].tag != VAR_UNKNOWN && getBookNrFromArg(&argvars[1], OUT &book) == FAIL)
      return;

   PropType* prop = findPropTypeByName(text(name), book);
   if (!prop)
      return;

   Bag *d = returnVar->bag;

   if (prop->hilite != SHORT)
      bagAddString(d, S"highlight", syn_id2name(prop->hilite));
   bagAddNumber(d, S"priority", prop->priority);
   bagAddNumber(d, S"combine", (prop->flags & PT_FLAG_COMBINE) ? 1 : 0);
   bagAddNumber(d, S"start_incl", (prop->flags & PT_FLAG_INS_START_INCL) ? 1 : 0);
   bagAddNumber(d, S"end_incl", (prop->flags & PT_FLAG_INS_END_INCL) ? 1 : 0);
   if (book)
      bagAddNumber(d, S"bufnr", book->fiNum);
}

private void
list_types(EeSet *ht, List *l) {
   long todo = (long)ht->count;
   EeSetItem   *hi;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
     if (!HASHITEM_EMPTY(hi)) {
        PropType *prop = HI2PT(hi);

        list_append_string(l, prop->name, -1);
        --todo;
      }
   }
}

// prop_type_list([{bufnr}])
pub void
f_prop_type_list(Var *argvars, OUT Var* returnVar) {
   allocReturnList(returnVar);

   Book* book = NULL;
   if (argvars[0].tag != VAR_UNKNOWN) {
      if (getBookNrFromArg(&argvars[0], &book) == FAIL)
          return;
   }
   if (!book) {
      if (global_proptypes != NULL)
         list_types(global_proptypes, returnVar->list);
   } ei (book->propTypes != NULL)
      list_types(book->propTypes, returnVar->list);
}

// Free all property types in "ht".
private void
clear_ht_prop_types(EeSet *ht) {
    long   todo;
    EeSetItem   *hi;

   if (!ht)
      return;

   todo = (long)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
          PropType *prop = HI2PT(hi);

          eeglFree(prop);
          --todo;
      }
   }

   hash_clear(ht);
   eeglFree(ht);
}

#if defined(EXITFREE)
// Free all global property types.
pub void
clear_global_prop_types(void) {
   clear_ht_prop_types(global_proptypes);
   global_proptypes = NULL;
   EE_CLEAR(global_proparray);
}
#endif

// Free all property types for "book".
private void
clearPropTypes(Book* book) {
   clear_ht_prop_types(book->propTypes);
   book->propTypes = NULL;
   EE_CLEAR(book->propArray);
}

//Adjust the property for "added" bytes (can be negative) inserted at "col".
//
//Note that "col" is zero-based, while col is one-based. Only for the current book.
//"flags" can have:
//APC_SUBSTITUTE:   Text is replaced, not inserted.
//APC_INDENT:      Text is inserted before virtual text prop
private AdjustRes
adjust(
   TextProp  *prop,
   ColNr       col,
   int       added,
   int       flags
) {
   PropType   *pt;
   int      start_incl;
   int      end_incl;
   int      droppable;
   AdjustRes res = {true, false};

   // prop after end of the line doesn't move
   if (prop->col == MAXCOL) {
      res.dirty = false;
      return res;
   }

   pt = text_prop_type_by_id(curBook, prop->type);
   start_incl = (pt != NULL && (pt->flags & PT_FLAG_INS_START_INCL))
            || (flags & APC_SUBSTITUTE)
            || (prop->flags & TEXT_PROP_CONT_PREV);
   if (prop->id < 0 && (flags & APC_INDENT))
      // when inserting indent just before a character with virtual text
      // shift the text property
      start_incl = false;
   end_incl = (pt != NULL && (pt->flags & PT_FLAG_INS_END_INCL))
            || (prop->flags & TEXT_PROP_CONT_NEXT);
   // do not drop zero-width props if they later can increase in size
   droppable = !(start_incl || end_incl);

   if (added > 0) {
      if (col + 1 <= prop->col - (start_incl || (prop->len == 0 && end_incl)))
         // Change is entirely before the text property: Only shift
         prop->col += added;
      ei (col + 1 < prop->col + prop->len + end_incl)
         // Insertion was inside text property
         prop->len += added;
   } ei (prop->col > col + 1) {
      if (prop->col + added < col + 1) {
         prop->len += (prop->col - 1 - col) + added;
         prop->col = col + 1;
         if (prop->len <= 0) {
            prop->len = 0;
            res.mayDrop = droppable;
         }
      }
      else
          prop->col += added;
   } ei (prop->len > 0 && prop->col + prop->len > col
       && prop->id >= 0  // don't change length for virtual text
   ) {
      int after = col - added - (prop->col - 1 + prop->len);

      prop->len += after > 0 ? added + after : added;
      res.mayDrop = prop->len <= 0 && droppable;
   } else
      res.dirty = false;

   return res;
}

//Adjust the columns of text properties in line "lnum" after position "col" to shift by 
//"bytes_added" (can be negative). Note that "col" is zero-based, while col is one-based.
//Only for the current book. "flags" can have:
//APC_SAVE_FOR_UNDO:   Call u_savesub() before making changes to the line.
//APC_SUBSTITUTE:   Text is replaced, not inserted.
//APC_INDENT:      Text is inserted before virtual text prop
//Caller is expected to check hasTextprop and "bytes_added" being non-zero.
//Return true when props were changed.
pub Boole
adjustPropColumns(LineNr lnum, ColNr col, int bytes_added, Unt flags) {
   if (textPropFrozenG > 0)
      return false;

   CS props;
   int proplen = get_text_props(OUT &props, curBook, lnum, true);
   if (proplen == 0)
      return false;
   Unt textlen = curBook->mem.lineLen - proplen * sizeof(TextProp);

   int wi = 0; // write index
   Boole dirty = false;
   for (int ri = 0; ri < proplen; ++ri) {
      TextProp   prop;
      AdjustRes   res;
      MEMMOVE(&prop, props + ri * sizeof(prop), sizeof(prop));
      res = adjust(&prop, col, bytes_added, flags);
      if (res.dirty) {
         // Save for undo if requested and not done yet.
         if ((flags & APC_SAVE_FOR_UNDO) && !dirty && u_savesub(lnum) == FAIL)
            return false;
         dirty = true;

         // u_savesub() may have updated curBook->mem, fetch it again
         if (curBook->mem.ml_line_lnum != lnum)
            proplen = get_text_props(OUT &props, curBook, lnum, true);
      }
      if (res.mayDrop)
         continue; // Drop this text property
      MEMMOVE(props + wi * sizeof(TextProp), &prop, sizeof(TextProp));
      ++wi;
   }
   if (dirty) {
      ColNr newlen = (int)textlen + wi * (ColNr)sizeof(TextProp);
      if ((curBook->mem.flags & ML_LINE_DIRTY) == 0) {
         CS p = eeMemsave(curBook->mem.cachedLine, newlen);
         curBook->mem.cachedLine = p;
      }
      curBook->mem.flags |= ML_LINE_DIRTY;
      curBook->mem.lineLen = newlen;
   }
   return dirty;
}

//Adjust text properties for a line that was split in two.
//"lnumProps" is the line that has the properties from before the split.
//"lnumTop" is the top line.
//"kept" is the number of bytes kept in the first line, while
//"deleted" is the number of bytes deleted.
//"atEol" is true if the split is after the end of the line.
pub void
adjustPropsForSplit(
   LineNr    lnumProps,
   LineNr    lnumTop,
   int       kept,
   int       deleted,
   int       atEol
){
   int skipped = kept + deleted;
   if (!curBook->hasTextprop)
      return;

   // Get the text properties from "lnumProps".
   CS props;
   int count = get_text_props(OUT &props, curBook, lnumProps, false);
   ArrayList prevProp;
   ArrayList nextProp;
   ga_init2(&prevProp, sizeof(TextProp), 10);
   ga_init2(&nextProp, sizeof(TextProp), 10);

   // Keep the relevant ones in the first line, reducing the length if needed.
   // Copy the ones that include the split to the second line.
   // Move the ones after the split to the second line.
   for (int i = 0; i < count; ++i) {
      // copy the prop to an aligned structure
      TextProp  prop;
      MEMMOVE(&prop, props + i * sizeof(TextProp), sizeof(TextProp));

      PropType* proTy = text_prop_type_by_id(curBook, prop.type);
      Boole startIncl = (proTy && (proTy->flags & PT_FLAG_INS_START_INCL));
      Boole endIncl = (proTy && (proTy->flags & PT_FLAG_INS_END_INCL));

      // a text prop "above" behaves like it is on the first text column
      int propCol = (prop.flags & TEXT_PROP_ALIGN_ABOVE) ? 1 : prop.col;

      Boole contPrev, contNext;
      if (propCol == MAXCOL) {
         contPrev = atEol;
         contNext = !atEol;
      } else {
         contPrev = propCol + (startIncl ? 0 : 1) <= kept;
         contNext = skipped <= propCol + prop.len - !endIncl;
      }
      // when a prop has text it is never copied
      if (prop.id < 0 && contNext)
         contPrev = false;

      if (contPrev && ga_grow(&prevProp, 1) == OK) {
         TextProp* tProp = ((TextProp *)prevProp.c) + prevProp.len;

         *tProp = prop;
         ++prevProp.len;
         if (tProp->col != MAXCOL && tProp->col + tProp->len >= kept)
            tProp->len = kept - tProp->col;
         if (contNext)
            tProp->flags |= TEXT_PROP_CONT_NEXT;
      }

      // Only add the property to the next line if the length is positive
      if (contNext && ga_grow(&nextProp, 1) == OK) {
         TextProp* tProp = ((TextProp *)nextProp.c) + nextProp.len;

         *tProp = prop;
         ++nextProp.len;
         if (tProp->col != MAXCOL) {
            if (tProp->col > skipped)
               tProp->col -= skipped - 1;
            else {
               tProp->len -= skipped - tProp->col;
               tProp->col = 1;
            }
         }
         if (contPrev)
            tProp->flags |= TEXT_PROP_CONT_PREV;
      }
    }

    set_text_props(lnumTop, prevProp.c, prevProp.len * sizeof(TextProp));
    ga_clear(&prevProp);
    set_text_props(lnumTop + 1, nextProp.c, nextProp.len * sizeof(TextProp));
    ga_clear(&nextProp);
}

// Prepend properties of joined line "lnum" to "new_props".
pub void
prepend_joined_props(
   CS new_props,
   int       propcount,
   OUT int* props_remaining,
   LineNr    lnum,
   int       last_line,
   long       col,
   int       removed
) {
   CS props;
   int proplen = get_text_props(OUT &props, curBook, lnum, false);
   for (int i = proplen; i-- > 0; ) {
      TextProp  prop;

      MEMMOVE(&prop, props + i * sizeof(prop), sizeof(prop));
      if (prop.col == MAXCOL && !last_line)
          continue;  // drop property with text after the line
      int end = !(prop.flags & TEXT_PROP_CONT_NEXT);

      adjust(&prop, 0, -removed, 0); // Remove leading spaces
      adjust(&prop, -1, col, 0); // Make line start at its final column

      if (last_line || end)
         MEMMOVE(new_props + --(*props_remaining) * sizeof(prop), &prop, sizeof(prop));
      else {
         Boole found = false;

         // Search for continuing prop.
         for (int j = *props_remaining; j < propcount; ++j) {
            TextProp op;

            MEMMOVE(&op, new_props + j * sizeof(op), sizeof(op));
            if ((op.flags & TEXT_PROP_CONT_PREV)
               && op.id == prop.id && op.type == prop.type
            ){
               found = true;
               op.len += op.col - prop.col;
               op.col = prop.col;
               // Start/end is taken care of when deleting joined lines
               op.flags = prop.flags;
               MEMMOVE(new_props + j * sizeof(op), &op, sizeof(op));
               break;
            }
         }
         if (!found)
            internal_error(S"text property above joined line not found");
      }
   }
}

//}}}
