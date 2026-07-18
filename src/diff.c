//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## diff.c: code for diffing two, three or four books.

//There are three ways to diff:
//- Shell out to an external diff program, using files.
//- Use the compiled-in xdiff library.
//- Let 'diffexpr' do the work, using files.

#include "eegl.h"
int stat(const char* restrict path, struct stat* restrict buf); // from sys/stat.h

struct DiffBlock {
   DiffBlock* df_next;
   LineNr   lnum[DB_COUNT];   // line number in book
   LineNr   count[DB_COUNT];   // nr of inserted/changed lines
   Boole isLinematched;  //has the linematch algorithm run on this diff hunk to divide it into
           // smaller diff hunks?

   int      has_changes;      // has cached list of inline changes
   ArrayList   changes;      // list of inline changes (DifflineChange)
};

#define IGNORE_WHITESPACE (1 << 1)
#define IGNORE_WHITESPACE_CHANGE (1 << 2)
#define IGNORE_WHITESPACE_AT_EOL (1 << 3)
#define IGNORE_CR_AT_EOL (1 << 4)
#define WHITESPACE_FLAGS (IGNORE_WHITESPACE | \
               IGNORE_WHITESPACE_CHANGE | \
               IGNORE_WHITESPACE_AT_EOL | \
               IGNORE_CR_AT_EOL)

#define IGNORE_BLANK_LINES (1 << 7)

#define PATIENCE_DIFF (1 << 14)
#define XDF_HISTOGRAM_DIFF (1 << 15)
#define XDF_DIFF_ALGORITHM_MASK (PATIENCE_DIFF | XDF_HISTOGRAM_DIFF)
#define XDF_DIFF_ALG(x) ((x) & XDF_DIFF_ALGORITHM_MASK)

#define INDENT_HEURISTIC (1 << 23)

// xpparm_t.flags
#define NEED_MINIMAL (1 << 0)

// Allocate an array of nr zeroed out elements, return NULL on failure
#define XDL_CALLOC_ARRAY(p, nr)   ((p) = xdl_calloc(nr, sizeof(*(p))))

typedef struct s_mmfile {
   Byte* ptr;
   long size;
} MmFile;

typedef struct s_mmbuffer {
   Byte* ptr;
   long size;
} MmBuffer;

typedef struct s_xpparam {
   unsigned long flags;
   // See Documentation/diff-options.txt
   char **anchors;
   Unt anchors_nr;
} XpParam;

declStruct(ChaNode);
struct ChaNode {
   ChaNode* next;
   long icurr;
};

typedef struct s_chastore {
   ChaNode *head, *tail;
   long isize, nsize;
   ChaNode *ancur;
   ChaNode *sncur;
   long scurr;
} ChaStore;


typedef long (*FindFn)(
   CS line, long line_len, char* buffer, long buffer_size, void *priv
);

typedef int (*XdlEmitHunkConsumeFn)(
   long start_a, long count_a, long start_b, long count_b, void *cb_data
);


typedef struct s_xdemitconf {
   long ctxlen;
   long interhunkctxlen;
   unsigned long flags;
   FindFn find_func;
   void* find_func_priv;
   XdlEmitHunkConsumeFn hunk_func;
} XdEmitConf;


typedef struct s_xdemitcb {
   void *priv;
   int (*out_hunk)(void *,
         long old_begin, long old_nr,
         long new_begin, long new_nr,
         Byte* func, long funclen);
   int (*out_line)(void *, MmBuffer *, int);
} XdEmitCb;

declStruct(Record);
struct Record {
   Record* next;
   CS ptr;
   long size;
   unsigned long ha;
};

typedef struct s_xdfile {
   ChaStore rcha;
   long nrec;
   unsigned int hbits;
   Record **rhash;
   long dstart, dend;
   Record **recs;
   CS rchg;
   long *rindex;
   long nreff;
   unsigned long *ha;
} XdFile;

typedef struct s_xdfenv {
   XdFile xdf1, xdf2;
} XdfEnv;

//{{{forward declarations

private int xdl_diff(MmFile *mf1, MmFile *mf2, XpParam* xpp,
        XdEmitConf *xecfg, XdEmitCb *ecb);

//}}}

//{{{linematch algorithm

#define LN_MAX_BUFS 8
#define LN_DECISION_MAX 255  // pow(2, LN_MAX_BUFS(8)) - 1 = 255

// struct for running the diff linematch algorithm
declStruct(DiffCmpPath);
struct DiffCmpPath {
    // to keep track of the total score of this path
    int levScore;
    Unt pathInd;   // current index of this path
    int choiceMem[LN_DECISION_MAX + 1];
    int choice[LN_DECISION_MAX];
    // to keep track of this path traveled
    DiffCmpPath* decision[LN_DECISION_MAX];
    Unt optimalChoice;
};

private Unt unwrap_indexes(const int *values, const int *diff_len, const Unt ndiffs);
private Ulong test_charmatch_paths(DiffCmpPath *node, int lastdecision);
private int matching_chars(const MmFile *m1, const MmFile *m2);

private Unt
line_len(const MmFile *m) {
    Byte* s = m->ptr;
    Byte* end = memchr(s, '\n', (Unt)m->size);
    return end ? (Unt)(end - s) : (Unt)m->size;
}

#define MATCH_CHAR_MAX_LEN 800


/// Same as matching_chars but ignore whitespace
///
/// @param s1
/// @param s2
private int
matching_chars_iwhite(const MmFile *s1, const MmFile *s2) {
   // the newly processed strings that will be compared delete the white space characters
   MmFile sp[2];
   Byte p[2][MATCH_CHAR_MAX_LEN];

   for (int k = 0; k < 2; k++) {
      const   MmFile *s = k == 0 ? s1 : s2;
      Unt pi = 0;
      Unt slen = MIN(MATCH_CHAR_MAX_LEN - 1, line_len(s));

      for (Unt i = 0; i <= slen; i++) {
         Byte e = s->ptr[i];

         if (e != ' ' && e != '\t') {
            p[k][pi] = e;
            pi++;
         }
      }

      sp[k].ptr = p[k];
      sp[k].size = (int)pi;
   }

   return matching_chars(&sp[0], &sp[1]);
}

/// Return matching characters between "s1" and "s2" whilst respecting sequence order.
/// Consider the case of two strings 'AAACCC' and 'CCCAAA', the
/// return value from this function will be 3, either to match
/// the 3 C's, or the 3 A's.
///
/// Examples:
///   matching_chars("aabc", "acba")               -> 2  // 'a' and 'b' in common
///   matching_chars("123hello567", "he123ll567o") -> 8  // '123', 'll' and '567' in common
///   matching_chars("abcdefg", "gfedcba")         -> 1  // all characters in common,
///                                                      // but only at most 1 in sequence
///
/// @param m1
/// @param m2
private int
matching_chars(const MmFile *m1, const MmFile *m2) {
   Unt   s1len = MIN(MATCH_CHAR_MAX_LEN - 1, line_len(m1));
   Unt   s2len = MIN(MATCH_CHAR_MAX_LEN - 1, line_len(m2));
   Byte* s1 = m1->ptr;
   Byte* s2 = m2->ptr;
   int matrix[2][MATCH_CHAR_MAX_LEN] = { 0 };
   int icur = 1;  // save space by storing only two rows for i axis

   for (Unt i = 0; i < s1len; i++) {
      icur = (icur == 1 ? 0 : 1);
      int *e1 = matrix[icur];
      int *e2 = matrix[!icur];

      for (Unt j = 0; j < s2len; j++) {
         // skip char in s1
         if (e2[j + 1] > e1[j + 1])
            e1[j + 1] = e2[j + 1];
         // skip char in s2
         if (e1[j] > e1[j + 1])
            e1[j + 1] = e1[j];
         // compare char in s1 and s2
         if ((s1[i] == s2[j]) && (e2[j] + 1) > e1[j + 1])
            e1[j + 1] = e2[j] + 1;
      }
   }

   return matrix[icur][s2len];
}

/// count the matching characters between a variable number of strings "sp"
/// mark the strings that have already been compared to extract them later
/// without re-running the character match counting.
/// @param sp
/// @param fomvals
/// @param n
private int
count_n_matched_chars(MmFile **sp, const Unt n, int iwhite) {
   int matched_chars = 0;
   int matched = 0;

   for (Unt i = 0; i < n; i++) {
      for (Unt j = i + 1; j < n; j++) {
          if (sp[i]->ptr != NULL && sp[j]->ptr != NULL) {
         matched++;
         // TODO(lewis6991): handle whitespace ignoring higher up in the
         // stack
         matched_chars += iwhite ? matching_chars_iwhite(sp[i], sp[j])
                  : matching_chars(sp[i], sp[j]);
          }
      }
   }

   // prioritize a match of 3 (or more lines) equally to a match of 2 lines
   if (matched >= 2) {
      matched_chars *= 2;
      matched_chars /= matched;
   }

   return matched_chars;
}

private MmFile
fastforward_buf_to_lnum(MmFile s, LineNr lnum) {
   for (int i = 0; i < lnum - 1; i++) {

      CS line_end = memchr(s.ptr, '\n', (Unt)s.size);
      s.size = line_end ? (int)(s.size - (line_end - s.ptr)) : 0;
      s.ptr = line_end;
      if (!s.ptr)
         break;
      s.ptr++;
      s.size--;
   }

    return s;
}

/// try all the different ways to compare these lines and use the one that
/// results in the most matching characters
/// @param df_iters
/// @param paths
/// @param npaths
/// @param path_idx
/// @param choice
/// @param diffcmppath
/// @param diff_len
/// @param ndiffs
/// @param diff_blk
private void
try_possible_paths(
    const int      *df_iters,
    const Unt   *paths,
    const int      npaths,
    const int      path_idx,
    int         *choice,
    DiffCmpPath   *diffcmppath,
    const int      *diff_len,
    const Unt   ndiffs,
    const MmFile   **diff_blk,
    int         iwhite)
{
   if (path_idx == npaths) {
      if ((*choice) > 0) {
          int from_vals[LN_MAX_BUFS] = { 0 };
          const int *to_vals = df_iters;

          MmFile mm[LN_MAX_BUFS];  // stack memory for current_lines
          MmFile *current_lines[LN_MAX_BUFS];
          for (Unt k = 0; k < ndiffs; k++) {
            from_vals[k] = df_iters[k];
            // get the index at all of the places
            if ((*choice) & (1 << k)) {
                from_vals[k]--;
                mm[k] = fastforward_buf_to_lnum(*diff_blk[k], df_iters[k]);
            } else
                CLEAR_FIELD(mm[k]);
            current_lines[k] = &mm[k];
         }
         Unt unwrapped_idx_from = unwrap_indexes(from_vals, diff_len, ndiffs);
         Unt unwrapped_idx_to = unwrap_indexes(to_vals, diff_len, ndiffs);
         int matched_chars = count_n_matched_chars(current_lines, ndiffs, iwhite);
         int score = diffcmppath[unwrapped_idx_from].levScore + matched_chars;

         if (score > diffcmppath[unwrapped_idx_to].levScore) {
            diffcmppath[unwrapped_idx_to].pathInd = 1;
            diffcmppath[unwrapped_idx_to].decision[0] = &diffcmppath[unwrapped_idx_from];
            diffcmppath[unwrapped_idx_to].choice[0] = *choice;
            diffcmppath[unwrapped_idx_to].levScore = score;
         } ei (score == diffcmppath[unwrapped_idx_to].levScore) {
            Unt k = diffcmppath[unwrapped_idx_to].pathInd++;
            diffcmppath[unwrapped_idx_to].decision[k] = &diffcmppath[unwrapped_idx_from];
            diffcmppath[unwrapped_idx_to].choice[k] = *choice;
         }
      }
      return;
   }

   Unt bit_place = paths[path_idx];
   *(choice) |= (1 << bit_place);  // set it to 1
   try_possible_paths(df_iters, paths, npaths, path_idx + 1, choice,
      diffcmppath, diff_len, ndiffs, diff_blk, iwhite);
   *(choice) &= ~(1 << bit_place);  // set it to 0
   try_possible_paths(df_iters, paths, npaths, path_idx + 1, choice,
      diffcmppath, diff_len, ndiffs, diff_blk, iwhite);
}

/// unwrap indexes to access n dimensional tensor
/// @param values
/// @param diff_len
/// @param ndiffs
private Unt
unwrap_indexes(const int *values, const int *diff_len, const Unt ndiffs) {
   Unt num_unwrap_scalar = 1;

   for (Unt k = 0; k < ndiffs; k++)
      num_unwrap_scalar *= (Unt)diff_len[k] + 1;

   Unt path_idx = 0;
   for (Unt k = 0; k < ndiffs; k++) {
      num_unwrap_scalar /= (Unt)diff_len[k] + 1;

      int n = values[k];
      path_idx += num_unwrap_scalar * (Unt)n;
   }

   return path_idx;
}

/// populate the values of the linematch algorithm tensor, and find the best
/// decision for how to compare the relevant lines from each of the books at
/// each point in the tensor
/// @param df_iters
/// @param ch_dim
/// @param diffcmppath
/// @param diff_len
/// @param ndiffs
/// @param diff_blk
private void
populate_tensor(
    int         *df_iters,
    const Unt   ch_dim,
    DiffCmpPath   *diffcmppath,
    const int      *diff_len,
    const Unt   ndiffs,
    const MmFile   **diff_blk,
    int         iwhite)
{
   if (ch_dim == ndiffs) {
      int npaths = 0;
      Unt paths[LN_MAX_BUFS];

      for (Unt j = 0; j < ndiffs; j++) {
         if (df_iters[j] > 0) {
            paths[npaths] = j;
            npaths++;
         }
      }

      int choice = 0;
      Unt unwrapper_idx_to = unwrap_indexes(df_iters, diff_len, ndiffs);

      diffcmppath[unwrapper_idx_to].levScore = -1;
      try_possible_paths(df_iters, paths, npaths, 0, &choice, diffcmppath,
                  diff_len, ndiffs, diff_blk, iwhite);
      return;
   }

   for (int i = 0; i <= diff_len[ch_dim]; i++) {
      df_iters[ch_dim] = i;
      populate_tensor(df_iters, ch_dim + 1, diffcmppath, diff_len,
         ndiffs, diff_blk, iwhite);
   }
}

// algorithm to find an optimal alignment of lines of a diff block with 2 or
// more files. The algorithm is generalized to work for any number of files
// which corresponds to another dimension added to the tensor used in the algorithm
//
// for questions and information about the linematch algorithm please contact
// Jonathon White (jonathonwhite@protonmail.com)
//
//for explanation, a summary of the algorithm in 3 dimensions (3 files compared) follows
//
//The 3d case (for 3 books) of the algorithm implemented when diffopt 'linematch' is enabled. 
//The algorithm constructs a 3d tensor to compare a diff between 3 books. The dimensions of 
//the tensor are the length of the diff in each buffer plus 1 A path is constructed by
//moving from one edge of the cube/3d tensor to the opposite edge. Motions from one cell of the 
//cube to the next represent decisions. In a 3d cube, there are a total of 7 decisions that can 
//be made, represented by the enum df_path3_choice which is defined in buffer_defs.h a comparison 
//of buffer 0 and 1 represents a motion toward the opposite edge of the cube with components along 
//the 0 and 1 axes.  a comparison of buffer 0, 1, and 2 represents a motion toward the opposite 
//edge of the cube with components along the 0, 1, and 2 axes. A skip of buffer 0 represents a 
//motion along only the 0 axis. For each action, a point value is awarded, and the path is
//saved for reference later, if it is found to have been the optimal path. The optimal path has 
//the highest score. The score is calculated as the summation of the total characters matching 
//between all of the lines which were compared. The structure of the algorithm is that of a dynamic
//programming problem. We can calculate a point i,j,k in the cube as a function of i-1, j-1, 
//and k-1. To find the score and path at point i,j,k, we must determine which path we want
//to use, this is done by looking at the possibilities and choosing the one which results in the 
//local highest score. The total highest scored path is, then in the end represented by the cell 
//in the opposite corner from the start location. The entire algorithm consists of populating the 
//3d cube with the optimal paths from which it may have came.
//
//Optimizations:
//As the function to calculate the cell of a tensor at point i,j,k is a function of the cells at 
//i-1, j-1, k-1, the whole tensor doesn't need to be stored in memory at once. In the case of the 
//3d cube, only two slices (along k and j axis) are stored in memory. For the 2d matrix
//(for 2 files), only two rows are stored at a time. The next/previous slice (or row) is always 
//calculated from the other, and they alternate at each iteration. In the 3d case, 3 arrays are 
//populated to memorize the score (matched characters) of the 3 books, so a redundant 
//calculation of the scores does not occur param [out] [allocated] decisions return the length of 
//decisions
private Unt
linematch_nbuffers(
   const MmFile** diff_blk,
   const int* diff_len,
   const Unt   ndiffs,
   OUT int** decisions,
   int iwhite
) {
   assert(ndiffs <= LN_MAX_BUFS);

   Unt memsize = 1;
   Unt memsize_decisions = 0;
   for (Unt i = 0; i < ndiffs; i++) {
      assert(diff_len[i] >= 0);
      memsize *= (Unt)(diff_len[i] + 1);
      memsize_decisions += (Unt)diff_len[i];
   }

   // create the flattened path matrix
   DiffCmpPath *diffcmppath = lalloc(sizeof(DiffCmpPath) * memsize, true);

   // allocate memory here
   Unt n = (Unt)pow(2.0, (double)ndiffs);
   for (Unt i = 0; i < memsize; i++) {
      diffcmppath[i].levScore = 0;
      diffcmppath[i].pathInd = 0;
      for (Unt j = 0; j < n; j++)
         diffcmppath[i].choiceMem[j] = -1;
   }

   // memory for avoiding repetitive calculations of score
   int df_iters[LN_MAX_BUFS];
   populate_tensor(df_iters, 0, diffcmppath, diff_len, ndiffs, diff_blk, iwhite);

   const Unt u = unwrap_indexes(diff_len, diff_len, ndiffs);
   DiffCmpPath *startNode = &diffcmppath[u];

   *decisions = lalloc(sizeof(int) * memsize_decisions, true);

   Unt n_optimal = 0;
   test_charmatch_paths(startNode, 0);
   while (startNode->pathInd > 0) {
      Unt j = startNode->optimalChoice;
      (*decisions)[n_optimal++] = startNode->choice[j];
      startNode = startNode->decision[j];
   }
   // reverse array
   for (Unt i = 0; i < (n_optimal / 2); i++) {
      int tmp = (*decisions)[i];
      (*decisions)[i] = (*decisions)[n_optimal - 1 - i];
      (*decisions)[n_optimal - 1 - i] = tmp;
   }

   eeglFree(diffcmppath);

   return n_optimal;
}

// return the minimum amount of path changes from start to end
private Ulong
test_charmatch_paths(DiffCmpPath* node, int lastdecision) {
   // memoization
   if (node->choiceMem[lastdecision] == -1) {
      if (node->pathInd == 0)
         // we have reached the end of the tree
         node->choiceMem[lastdecision] = 0;
      else {
         // the minimum amount of turns required to reach the end
         Ulong minimum_turns = SIZE_MAX;
         for (Ulong i = 0; i < node->pathInd; i++) {
            // recurse
            Ulong t = test_charmatch_paths( node->decision[i], node->choice[i]) 
                        + (lastdecision != node->choice[i] ? 1 : 0);
            if (t < minimum_turns) {
               node->optimalChoice = i;
               minimum_turns = t;
            }
         }
         node->choiceMem[lastdecision] = (int)minimum_turns;
      }
   }

   return (Unt)node->choiceMem[lastdecision];
}

//}}}

private Boole isBusyP = false;     //using diff structs, don't change them
private Boole needUpdateP = false; //c_diffupdate needs to be called

// flags obtained from the @diffopt
#define DIFF_FILLER          0x001 //display filler lines
#define DIFF_IBLANK          0x002 //ignore empty lines
#define DIFF_ICASE           0x004 //ignore case
#define DIFF_IWHITE          0x008 //ignore change in white space
#define DIFF_IWHITEALL       0x010 //ignore all white space changes
#define DIFF_IWHITEEOL       0x020 //ignore change in white space at EOL
#define DIFF_HORIZONTAL      0x040 //horizontal splits
#define DIFF_VERTICAL        0x080 //vertical splits
#define DIFF_HIDDEN_OFF      0x100 //diffoff when hidden
#define DIFF_INTERNAL        0x200 //use internal xdiff algorithm
#define DIFF_CLOSE_OFF       0x400 //diffoff when closing portal
#define DIFF_FOLLOWWRAP      0x800 //follow the wrap option
#define DIFF_LINEMATCH      0x1000 //match most similar lines within diff
#define DIFF_INLINE_NONE    0x2000 //no inline highlight
#define DIFF_INLINE_SIMPLE  0x4000 //inline highlight with simple algorithm
#define DIFF_INLINE_CHAR    0x8000 //inline highlight with character diff
#define DIFF_INLINE_WORD   0x10000 //inline highlight with word diff
#define DIFF_ANCHOR        0x20000 //use @diffanchors to anchor the diff
#define ALL_WHITE_DIFF (DIFF_IWHITE | DIFF_IWHITEALL | DIFF_IWHITEEOL)
#define ALL_INLINE (DIFF_INLINE_NONE | DIFF_INLINE_SIMPLE | DIFF_INLINE_CHAR | DIFF_INLINE_WORD)
#define ALL_INLINE_DIFF (DIFF_INLINE_CHAR | DIFF_INLINE_WORD)
private Unt diff_flags = DIFF_INTERNAL | DIFF_FILLER | DIFF_CLOSE_OFF;

private long diff_algorithm = 0;

#define LBUFLEN 50      // length of line in diff file

private int diff_a_works = MAYBE; //true when "diff -a" works, false when it
                                  // doesn't work, MAYBE when not checked yet

#define MAX_DIFF_ANCHORS 20

// used for diff input
typedef struct {
   CS externalFname;  //for external diff
   MmFile mmfile;     //for internal diff
} DiffInp;

// used for diff DiffResult
typedef struct {
   CS outFname;       //for external diff
   ArrayList dout_ga; //for internal diff
} DiffResult;

// used for recording hunks from xdiff
typedef struct {
   LineNr origLnum;
   long origCount;
   LineNr newLnum;
   long newCount;
} Hunk;

typedef enum {
   DIO_OUTPUT_INDICES = 0, //default
   DIO_OUTPUT_UNIFIED = 1  //unified diff format
} OutputFormat;

// two diff inputs and one DiffResult
typedef struct {
   DiffInp orig;     // original file input
   DiffInp new;      // new file input
   DiffResult dio_diff;     //diff DiffResult
   int dio_internal; // using internal diff
   OutputFormat dio_outfmt;   //internal diff output format
   int dio_ctxlen;   // unified diff context length
} DiffIo;

private Unt bookIndex(Book *);
private Unt bookIndexInTab(Book *, Tab *);
private void diff_mark_adjust_tp(Tab *t, Unt idx, LineNr line1, LineNr line2, long amount, long amount_after);
private void diff_check_unchanged(Tab *t, DiffBlock *dp);
private int checkSanity(Tab* t, DiffBlock *dp);
private int check_external_diff(DiffIo *diffio);
private int diff_file(DiffIo *diffio);
private int diff_equal_entry(DiffBlock *dp, Unt idx1, Unt idx2);
private int diff_cmp(CS s1, CS s2);
private void diff_fold_update(DiffBlock *dp, Unt skip_idx);
private void diff_read(int iOrig, int iNew, DiffIo *dio);
private void diff_copy_entry(DiffBlock *dprev, DiffBlock *dp, int iOrig, int iNew);
private DiffBlock *diff_alloc_new(Tab *t, DiffBlock *dprev, DiffBlock *dp);
private int parse_diff_ed(Byte *line, Hunk *hunk);
private int parse_diff_unified(Byte *line, Hunk *hunk);
private int xdiff_out_indices(long start_a, long count_a, long start_b, long count_b, void *priv);
private int xdiff_out_unified(void *priv, MmBuffer *mb, int nbuf);
private int parse_diffanchors(
      CS diffAnchors, Boole check_only, Book* book, LineNr *anchors, OUT Unt *countAanchors
);

#define FOR_ALL_DIFFBLOCKS_IN_TAB(t, dp) \
    for ((dp) = (t)->first_diff; (dp) != NULL; (dp) = (dp)->df_next)

private void
clear_diffblock(DiffBlock *dp) {
   ga_clear(&dp->changes);
   eeglFree(dp);
}

// Called when deleting or unloading a book: No longer make a diff with it.
void
diffDeleteBook(Book* book) {
   Tab* t;
   FOR_ALL_TABS(t) {
      Unt i = bookIndexInTab(book, t);
      if (i != UNT) {
         t->diffbuf[i] = NULL;
         t->diff_invalid = true;
         if (t == curtab) {
            // don't redraw right away, more might change or book state is invalid right now
            diffNeedsRedrawG = true;
            redraw_later(UPD_VALID);
         }
      }
   }
}

// Check if the current book should be added to or removed from the list of diff books.
void
diffBookAdjust(Portal* port) {
   if (!port->o.diff) {
      // When there is no portal showing a diff for this book, remove it from the diffs.
      Portal* po;
      FOR_ALL_PORTALS(po) {
         if (po->book == port->book && po->o.diff)
            break;
      } 
      if (!po) {
         Unt i = bookIndex(port->book);
         if (i != UNT) {
            curtab->diffbuf[i] = NULL;
            curtab->diff_invalid = true;
            diff_redraw(true);
         }
      }
   } else
      diffAddBook(port->book);
}

//Add a book to make diffs for.
//Call this when a new book is being edited in the current portal where @diff is set.
//Mark the current book as being part of the diff and requiring updating.
//This must be done before any autocmd, because a command may use info about the screen contents.
void
diffAddBook(Book* book) {
   if (bookIndex(book) != UNT)
      return;      // It's already there.

   for (Unt i = 0; i < DB_COUNT; ++i) {
      if (curtab->diffbuf[i] == NULL) {
         curtab->diffbuf[i] = book;
         curtab->diff_invalid = true;
         diff_redraw(true);
         return;
      }
   } 

   showErrFmtMsg(_(e_cannot_diff_more_than_nr_buffers), DB_COUNT);
}

// Remove all books to make diffs for.
private void
clearAllBooks(void) {
   for (Unt i = 0; i < DB_COUNT; ++i) {
      if (curtab->diffbuf[i] != NULL) {
         curtab->diffbuf[i] = NULL;
         curtab->diff_invalid = true;
         diff_redraw(true);
      }
   } 
}

//Find book in the list of diff books for the current tab.
//Return its index or DB_COUNT if not found.
private Unt
bookIndex(Book* book) {
   for (Unt idx = 0; idx < DB_COUNT; ++idx) {
      if (curtab->diffbuf[idx] == book)
         return idx;
   } 
   return UNT;
}

//Find book "book" in the list of diff books for tab "t". 
//Return its index or DB_COUNT if not found
private Unt
bookIndexInTab(Book* book, Tab* t) {
   for (Unt idx = 0; idx < DB_COUNT; ++idx) {
      if (t->diffbuf[idx] == book)
         return idx;
   } 
   return UNT;
}

// Mark the diff info involving book as invalid, it will be updated when info is requested.
void
diff_invalidate(Book* book) {
   Tab* t;
   FOR_ALL_TABS(t) {
      Unt i = bookIndexInTab(book, t);
      if (i != UNT) {
         t->diff_invalid = true;
         if (t == curtab)
            diff_redraw(true);
      }
   }
}

// Called by markAdjust(): update line numbers in "curBook".
void
diff_mark_adjust(LineNr line1, LineNr line2, long amount, long amount_after){
   Tab   *t;
   // Handle all tabs that use the current book in a diff.
   FOR_ALL_TABS(t) {
      Unt idx = bookIndexInTab(curBook, t);
      if (idx != UNT)
         diff_mark_adjust_tp(t, idx, line1, line2, amount, amount_after);
    }
}

//Update line numbers in tab "t" for "curBook" with index "idx".
//This attempts to update the changes as much as possible:
//When inserting/deleting lines outside of existing change blocks, create a
//new change block and update the line numbers in following blocks.
//When inserting/deleting lines in existing change blocks, update them.
private void
diff_mark_adjust_tp(
   Tab* t,
   Unt idx,
   LineNr line1,
   LineNr line2,
   long amount,
   long amount_after
) {
   DiffBlock* dnext;
   int      inserted, deleted;
   int      n, off;
   LineNr   last;
   LineNr   lnum_deleted = line1;   // lnum of remaining deletion
   Boole      check_unchanged;

   if (diff_internal()) {
      //Will update diffs before redrawing.  Set _invalid to update the
      //diffs themselves, set _update to also update folds properly just before redrawing.
      //Do update marks here, it is needed for :%diffput.
      t->diff_invalid = true;
      t->diff_update = true;
   }

   if (line2 == MAXLNUM) {
      // markAdjust(99, MAXLNUM, 9, 0, true): insert lines
      inserted = amount;
      deleted = 0;
   } ei (amount_after > 0) {
      // markAdjust(99, 98, MAXLNUM, 9, true): a change that inserts lines
      inserted = amount_after;
      deleted = 0;
   } else {
      // markAdjust(98, 99, MAXLNUM, -2, true): delete lines
      inserted = 0;
      deleted = -amount_after;
   }

   DiffBlock* dprev = NULL;
   DiffBlock* dp = t->first_diff;
   for (;;) {
      // If the change is after the previous diff block and before the next
      // diff block, thus not touching an existing change, create a new diff
      // block.  Don't do this when c_diffgetput() is busy.
      if ((!dp || dp->lnum[idx] - 1 > line2
             || (line2 == MAXLNUM && dp->lnum[idx] > line1))
         && (!dprev || dprev->lnum[idx] + dprev->count[idx] < line1)
         && !isBusyP
      ) {
         dnext = diff_alloc_new(t, dprev, dp);
         if (!dnext)
            return;

         dnext->lnum[idx] = line1;
         dnext->count[idx] = inserted;
         for (Unt i = 0; i < DB_COUNT; ++i) {
            if (t->diffbuf[i] != NULL && i != idx) {
               if (dprev == NULL)
                  dnext->lnum[i] = line1;
               else
                  dnext->lnum[i] = line1
                      + (dprev->lnum[i] + dprev->count[i])
                      - (dprev->lnum[idx] + dprev->count[idx]);
               dnext->count[i] = deleted;
            }
         }
      }

      // if at end of the list, quit
      if (dp == NULL)
         break;

      // Check for these situations:
      //     1  2   3
      //     1  2   3
      // line1     2   3  4  5
      //        2   3  4  5
      //        2   3  4  5
      // line2     2   3  4  5
      //      3     5  6
      //      3     5  6
      //
      // compute last line of this change
      last = dp->lnum[idx] + dp->count[idx] - 1;

      // 1. change completely above line1: nothing to do
      if (last >= line1 - 1) {
         if (isBusyP) {
            //Currently in the middle of updating diff blocks. All we want
            //is to adjust the line numbers and nothing else.
            if (dp->lnum[idx] > line2)
                dp->lnum[idx] += amount_after;

            //Advance to next entry.
            dprev = dp;
            dp = dp->df_next;
            continue;
         }

         // 6. change below line2: only adjust for amount_after; also when
         // "deleted" became zero when deleted all lines between two diffs
         if (dp->lnum[idx] - (deleted + inserted != 0) > line2) {
            if (amount_after == 0)
               break;   // nothing left to change
            dp->lnum[idx] += amount_after;
         } else {
            check_unchanged = false;

            // 2. 3. 4. 5.: inserted/deleted lines touching this diff.
            if (deleted > 0) {
               off = 0;
               if (dp->lnum[idx] >= line1) {
                  if (last <= line2) {
                     // 4. delete all lines of diff
                     if (dp->df_next != NULL && dp->df_next->lnum[idx] - 1 <= line2) {
                        // delete continues in next diff, only do lines until that one
                        n = dp->df_next->lnum[idx] - lnum_deleted;
                        deleted -= n;
                        n -= dp->count[idx];
                        lnum_deleted = dp->df_next->lnum[idx];
                     } else
                        n = deleted - dp->count[idx];
                     dp->count[idx] = 0;
                  } else {
                     // 5. delete lines at or just before top of diff
                     off = dp->lnum[idx] - lnum_deleted;
                     n = off;
                     dp->count[idx] -= line2 - dp->lnum[idx] + 1;
                     check_unchanged = true;
                  }
                  dp->lnum[idx] = line1;
               } else {
                  if (last < line2) {
                     // 2. delete at end of diff
                     dp->count[idx] -= last - lnum_deleted + 1;
                     if (dp->df_next && dp->df_next->lnum[idx] - 1 <= line2) {
                        //delete continues in next diff, only do lines until that one
                        n = dp->df_next->lnum[idx] - 1 - last;
                        deleted -= dp->df_next->lnum[idx] - lnum_deleted;
                        lnum_deleted = dp->df_next->lnum[idx];
                     } else
                        n = line2 - last;
                     check_unchanged = true;
                  } else {
                      // 3. delete lines inside the diff
                      n = 0;
                      dp->count[idx] -= deleted;
                  }
               }

               for (Unt i = 0; i < DB_COUNT; ++i) {
                  if (t->diffbuf[i] != NULL && i != idx) {
                     if (dp->lnum[i] > off)
                        dp->lnum[i] -= off;
                     else
                        dp->lnum[i] = 1;
                     dp->count[i] += n;
                  }
               } 
            } else {
               if (dp->lnum[idx] <= line1) {
                  // inserted lines somewhere in this diff
                  dp->count[idx] += inserted;
                  check_unchanged = true;
               } else
                  // inserted lines somewhere above this diff
                  dp->lnum[idx] += inserted;
            }

            if (check_unchanged)
               //Check if inserted lines are equal, may reduce the size of the diff. TODO: also 
               //check for equal lines in the middle and perhaps split the block.
               diff_check_unchanged(t, dp);
          }
      }

      // check if this block touches the previous one, may merge them.
      if (dprev && !dp->isLinematched && !isBusyP
               && dprev->lnum[idx] + dprev->count[idx] == dp->lnum[idx]
      ) {
         for (Unt i = 0; i < DB_COUNT; ++i) {
            if (t->diffbuf[i])
               dprev->count[i] += dp->count[i];
         } 
         dprev->df_next = dp->df_next;
         clear_diffblock(dp);
         dp = dprev->df_next;
      } else {
         // Advance to next entry.
         dprev = dp;
         dp = dp->df_next;
      }
   }

   dprev = NULL;
   dp = t->first_diff;
   while (dp) {
      // All counts are zero, remove this entry.
      Unt i;
      for (i = 0; i < DB_COUNT; ++i) {
         if (t->diffbuf[i] != NULL && dp->count[i] != 0)
            break;
      } 
      if (i == DB_COUNT) {
         dnext = dp->df_next;
         clear_diffblock(dp);
         dp = dnext;
         if (!dprev)
            t->first_diff = dnext;
         else
            dprev->df_next = dnext;
      } else {
         // Advance to next entry.
         dprev = dp;
         dp = dp->df_next;
      }

    }

   if (t == curtab) {
      // Don't redraw right away, this updates the diffs, which can be slow.
      diffNeedsRedrawG = true;

      //Need to recompute the scroll binding, may remove or add filler lines (e.g., when adding 
      //lines above topLine). But it's slow when making many changes, postpone until redrawing.
      diff_need_scrollbind = true;
   }
}

//Allocate a new diff block and link it between "dprev" and "dp".
private DiffBlock*
diff_alloc_new(Tab* t, DiffBlock* dprev, DiffBlock* dp) {
   DiffBlock* dnew = ALLOC_CLEAR_ONE(DiffBlock);
   dnew->isLinematched = false;
   dnew->df_next = dp;
   if (!dprev)
      t->first_diff = dnew;
   else
      dprev->df_next = dnew;

   dnew->has_changes = false;
   ga_init2(&dnew->changes, sizeof(DifflineChange), 20);
   return dnew;
}

//Check if the diff block "dp" can be made smaller for lines at the start and end that are equal.
//Called after inserting lines. This may DiffResult in a change where all books have 0 lines, 
//the caller must take care of removing it.
private void
diff_check_unchanged(Tab* t, DiffBlock *dp) {
   CS line_org;
   Unt dir = FORWARD;

   // Find the first book, use it as the original, compare the other book lines against this one.
   Unt i_org;
   for (i_org = 0; i_org < DB_COUNT; ++i_org) {
      if (t->diffbuf[i_org] != NULL)
         break;
   } 
   if (i_org == DB_COUNT)   // safety check
      return;

   if (checkSanity(t, dp) == FAIL)
      return;

   // First check lines at the top, then at the bottom.
   int off_org = 0;
   int off_new = 0;
   for (;;) {
      //Repeat until a line is found which is different or the number of lines has become zero.
      while (dp->count[i_org] > 0) {
         // Copy the line, the next ml_get() will invalidate it.
         if (dir == BACKWARD)
            off_org = dp->count[i_org] - 1;
         line_org = copyStr(
               memGetLine(t->diffbuf[i_org], dp->lnum[i_org] + off_org, false)
         );
         Unt i_new;
         for (i_new = i_org + 1; i_new < DB_COUNT; ++i_new) {
            if (t->diffbuf[i_new] == NULL)
               continue;
            if (dir == BACKWARD)
               off_new = dp->count[i_new] - 1;
            // if other book doesn't have this line, it was inserted
            if (off_new < 0 || off_new >= dp->count[i_new])
               break;
            if (diff_cmp(line_org, memGetLine(t->diffbuf[i_new],
                     dp->lnum[i_new] + off_new, false)) != 0)
               break;
         }
         eeglFree(line_org);

         //Stop when a line isn't equal in all diff books.
         if (i_new != DB_COUNT)
            break;

         //Line matched in all books, remove it from the diff.
         for (Unt i_new = i_org; i_new < DB_COUNT; ++i_new) {
            if (t->diffbuf[i_new] != NULL) {
               if (dir == FORWARD)
                  ++dp->lnum[i_new];
               --dp->count[i_new];
            }
         } 
      }
      if (dir == BACKWARD)
         break;
      dir = BACKWARD;
    }
}

//Check if a diff block doesn't contain invalid line numbers.
//This can happen when the diff program returns invalid results.
private int
checkSanity(Tab* t, DiffBlock *dp) {
   for (Unt i = 0; i < DB_COUNT; ++i) {
      if (t->diffbuf[i] != NULL) {
         if (dp->lnum[i] + dp->count[i] - 1 > t->diffbuf[i]->mem.lineCount)
            return FAIL;
      } 
   }
   return OK;
}

//Mark all diff books in the current tab for redraw.
void
diff_redraw(int dofold)  {    // also recompute the folds
   Portal   *po;
   Portal   *other = NULL;
   int used_max_fill_other = false;
   int used_max_fill_curPor = false;
   int n;

   diffNeedsRedrawG = false;
   FOR_ALL_PORTALS(po) {
      // when closing portals or wiping books skip invalid portal
      if (!po->o.diff || !bookIsValid(po->book))
          continue;

      redrawPortLater(po, UPD_SOME_VALID);
      if (po != curPor)
          other = po;
      if (dofold && po->o.foldMethod == FOLD_DIFF)
          foldUpdateAll(po);
      //A change may have made filler lines invalid, need to take care of
      //that for other portals.
      n = diff_check_fill(po, po->topLine);
      if ((po != curPor && po->topFill > 0) || n > 0) {
         if (po->topFill > n)
            po->topFill = (n < 0 ? 0 : n);
         ei (n > 0 && n > po->topFill) {
            po->topFill = n;
        if (po == curPor)
            used_max_fill_curPor = true;
        ei (other != NULL)
            used_max_fill_other = true;
        }
        check_topfill(po, false);
     }
  }

  if (other && curPor->o.diff) {
      if (used_max_fill_curPor)
         //The current portal was set to use the maximum number of filler
         //lines, may need to reduce them.
         diff_set_topline(other, curPor);
      ei (used_max_fill_other)
         //The other portal was set to use the maximum number of filler
         //lines, may need to reduce them.
         diff_set_topline(curPor, other);
   }
}

private void
clear_diffin(DiffInp* din) {
   if (din->externalFname == NULL)
      EE_CLEAR(din->mmfile.ptr);
   else
      mch_remove(din->externalFname);
}

private void
clear_diffout(DiffResult* dout) {
   if (dout->outFname == NULL)
      ga_clear_strings(&dout->dout_ga);
   else
      mch_remove(dout->outFname);
}

//Write "book" to a memory buffer. Return FAIL for failure.
private int
diff_write_buffer(Book* book, DiffInp* din, LineNr start, LineNr end) {
   if (end < 0)
      end = book->mem.lineCount;

   if (book->mem.flags & ML_EMPTY || end < start) {
      din->mmfile.ptr = NULL;
      din->mmfile.size = 0;
      return OK;
   }

   //xdiff requires one big block of memory with all the text.
   long len = 0;
   LineNr lnum;
   for (lnum = start; lnum <= end; ++lnum)
      len += memGetBookLen(book, lnum) + 1;
   CS ptr = tryBigAlloc(len);
   if (!ptr) {
      //Allocating memory failed. This can happen, because we try to read
      //the whole book text into memory. Set the failed flag, the diff
      //will be retried with external diff. The flag is never reset.
      book->diffFailed = true;
      if (p_verbose > 0) {
         verbose_enter();
         smsg(_("Not enough memory to use internal diff for book \"%s\""), book->currFileName);
         verbose_leave();
      }
      return FAIL;
   }
   din->mmfile.ptr = ptr;
   din->mmfile.size = len;

   len = 0;
   CS s;
   for (lnum = start; lnum <= end; ++lnum) {
      for (s = memGetLine(book, lnum, false); *s != ZERO; ) {
         if (diff_flags & DIFF_ICASE) {
            int   orig_len;
            int   c_len = 1;
            Byte cbuf[MB_MAXBYTES + 1];

            Unt c;
            if (*s == NL)
               c = ZERO;
            else {
               // xdiff doesn't support ignoring case, fold-case the text.
               c = mb_ptr2char(s);
               c_len = MB_CHAR2LEN(c);
               c = MB_CASEFOLD(c);
            }
            orig_len = utfCharLen(s);
            if (mb_char2bytes(c, cbuf) != c_len)
               // TODO: handle byte length difference. One example is Å (3 bytes) and å (2 bytes)
               MEMMOVE(ptr + len, s, orig_len);
            else {
               MEMMOVE(ptr + len, cbuf, c_len);
               if (orig_len > c_len) {
                  // Copy remaining composing characters
                  MEMMOVE(ptr + len + c_len, s + c_len, orig_len - c_len);
               }
            }

            s += orig_len;
            len += orig_len;
         } else {
            ptr[len++] = *s == NL ? ZERO : *s;
            s++;
         }
      }
      ptr[len++] = NL;
   }
   return OK;
}

//Write "book" to file or memory buffer. Return FAIL for failure or NOTDONE for refusal to save.
private int
diff_write(Book* book, DiffInp* din, LineNr start, LineNr end) {
   if (din->externalFname == NULL)
      return diff_write_buffer(book, din, start, end);
      
   if (end < 0)
      end = book->mem.lineCount;

   int save_ml_flags = book->mem.flags;
   int save_cmod_flags = commModifierG.cmod_flags;
   // Writing the buffer is an implementation detail of performing the diff,
   // so it shouldn't update the '[ and '] marks.
   commModifierG.cmod_flags |= CMOD_LOCKMARKS;
   if (end < start) {
      // The line range specifies a completely empty file.
      end = start;
      book->mem.flags |= ML_EMPTY;
   }
   int r = bookWrite(book, din->externalFname, NULL, start, end, NULL, false, false, false, true);
   commModifierG.cmod_flags = save_cmod_flags;
   book->mem.flags = save_ml_flags;
   return r;
}

private int
lnum_compare(const void *s1, const void *s2) {
   LineNr lnum1 = *(LineNr*)s1;
   LineNr lnum2 = *(LineNr*)s2;
   if (lnum1 < lnum2)
      return -1;
   if (lnum1 > lnum2)
      return 1;
   return 0;
}

//Update the diffs for all books involved.
private void
diff_try_update(DiffIo* dio, int iOrig, NULLABLE Invocation* invo) {
   if (dio->dio_internal) {
      ga_init2(&dio->dio_diff.dout_ga, sizeof(char *), 1000);
   } else {
      // We need three temp file names.
      dio->orig.externalFname = eeTempName('o', true);
      dio->new.externalFname = eeTempName('n', true);
      dio->dio_diff.outFname = eeTempName('d', true);
      if (!dio->orig.externalFname || !dio->new.externalFname || !dio->dio_diff.outFname)
         goto theend;
   }

   // Check external diff is actually working.
   if (!dio->dio_internal && check_external_diff(dio) == FAIL)
      goto theend;

   // :diffupdate!
   Book* book;
   if (invo && invo->forceit) {
      for (Unt iNew = iOrig; iNew < DB_COUNT; ++iNew) {
         book = curtab->diffbuf[iNew];
         if (bookIsValid(book))
            fiCheckBookTimestamp(book);
      }
   }

   // Parse and sort diff anchors if enabled
   Unt countAnchors = UNT;
   LineNr anchors[DB_COUNT][MAX_DIFF_ANCHORS];
   CLEAR_FIELD(anchors);
   if ((diff_flags & DIFF_ANCHOR) != 0) {
      for (Unt idx = 0; idx < DB_COUNT; idx++) {
         if (curtab->diffbuf[idx] == NULL)
            continue;
         Unt bufCountAnchors = 0;
         if (parse_diffanchors(
                  curBook->o.diffAnchors, false, curtab->diffbuf[idx], anchors[idx], OUT &bufCountAnchors
             ) != OK
         ){
            emsg(_(e_failed_to_find_all_diff_anchors));
            countAnchors = 0;
            CLEAR_FIELD(anchors);
            break;
         }
         if (bufCountAnchors < countAnchors)
            countAnchors = bufCountAnchors;

         if (bufCountAnchors > 0)
            qsort((void *)anchors[idx], bufCountAnchors, sizeof(LineNr), lnum_compare);
      }
   }
   if (countAnchors == UNT)
      countAnchors = 0;

   //Split the files into multiple sections by anchors. Each section starts
   //from one anchor (inclusive) and ends at the next anchor (exclusive).
   //Diff each section separately before combining the results. If we don't
   //have any anchors, we will have one big section of the entire file.
   for (Unt anchorInd = 0; anchorInd <= countAnchors; anchorInd++) {
      DiffBlock* orig_diff = NULL;
      if (anchorInd != 0) {
         orig_diff = curtab->first_diff;
         curtab->first_diff = NULL;
      }
      LineNr lnum_start = (anchorInd == 0) ? 1 : anchors[iOrig][anchorInd - 1];
      LineNr lnum_end = (anchorInd == countAnchors) ? -1 : anchors[iOrig][anchorInd] - 1;

      //Write the first book to a tempfile or MmFile.
      book = curtab->diffbuf[iOrig];
      int writeResult = diff_write(book, &dio->orig, lnum_start, lnum_end);
      if (writeResult == NOTDONE) {
         emsg(_(e_cannot_make_changes_modifiable_is_off));
      }
      if (writeResult != OK) {
         if (orig_diff) {
            //Clean up in-progress diff blocks
            curtab->first_diff = orig_diff;
            diff_clear(curtab);
         }
         goto theend;
      }

      // Make a difference between the first book and every other.
      for (Unt iNew = iOrig + 1; iNew < DB_COUNT; ++iNew) {
         book = curtab->diffbuf[iNew];
         if (!book || book->mem.mfile == NULL)
            continue; // skip book that isn't loaded

         lnum_start = anchorInd == 0 ? 1 : anchors[iNew][anchorInd - 1];
         lnum_end = anchorInd == countAnchors ? -1 : anchors[iNew][anchorInd] - 1;

         // Write the other book and diff with the first one.
         if (diff_write(book, &dio->new, lnum_start, lnum_end) == FAIL)
            continue;
         if (diff_file(dio) == FAIL)
            continue;

         // Read the diff output and add each entry to the diff list.
         diff_read(iOrig, iNew, dio);

         clear_diffin(&dio->new);
         clear_diffout(&dio->dio_diff);
      }
      clear_diffin(&dio->orig);

      if (anchorInd != 0) {
         // Combine the new diff blocks with the existing ones
         for (DiffBlock *dp = curtab->first_diff; dp != NULL; dp = dp->df_next) {
            for (Unt idx = 0; idx < DB_COUNT; idx++) {
               if (anchors[idx][anchorInd - 1] > 0)
                  dp->lnum[idx] += anchors[idx][anchorInd - 1] - 1;
            }
         }
         if (orig_diff) {
            DiffBlock* last_diff = orig_diff;
            while (last_diff->df_next != NULL)
               last_diff = last_diff->df_next;
            last_diff->df_next = curtab->first_diff;
            curtab->first_diff = orig_diff;
         }
      }
   }

theend:
   eeglFree(dio->orig.externalFname);
   eeglFree(dio->new.externalFname);
   eeglFree(dio->dio_diff.outFname);
}

//Return true if the options are set to use the internal diff library.
//Note that if the internal diff failed for one of the books, the external diff will be used anyway.
int
diff_internal(void) {
   return (diff_flags & DIFF_INTERNAL) != 0 && !p_dex;
}

//Return true if the internal diff failed for one of the diff books.
private int
diff_internal_failed(void) {
   // Only need to do something when there is another book.
   for (Unt idx = 0; idx < DB_COUNT; ++idx) {
      if (curtab->diffbuf[idx] != NULL && curtab->diffbuf[idx]->diffFailed)
         return true;
   } 
   return false;
}

//Completely update the diffs for the books involved.
//When using the external "diff" command the books are written to a file,
//also for unmodified books (the file could have been produced by
//autocommands, e.g. the netrw plugin).
void
c_diffupdate(Invocation* invo) {  // "invo" can be NULL
   if (isBusyP) {
      needUpdateP = true;
      return;
   }

   // Delete all diffblocks.
   diff_clear(curtab);
   curtab->diff_invalid = false;

   // Use the first book as the original text.
   Unt iOrig;
   for (iOrig = 0; iOrig < DB_COUNT; ++iOrig) {
      if (curtab->diffbuf[iOrig] != NULL)
         break;
   } 
   if (iOrig == DB_COUNT)
      goto theend;

   // Only need to do something when there is another book.
   Unt iNew;
   for (iNew = iOrig + 1; iNew < DB_COUNT; ++iNew) {
      if (curtab->diffbuf[iNew] != NULL)
          break;
   } 
   if (iNew == DB_COUNT)
      goto theend;

   // Only use the internal method if it did not fail for one of the books.
   DiffIo diffio;
   CLEAR_FIELD(diffio);
   diffio.dio_internal = diff_internal() && !diff_internal_failed();

   diff_try_update(&diffio, iOrig, invo);
   if (diffio.dio_internal && diff_internal_failed()) {
      // Internal diff failed, use external diff instead.
      CLEAR_FIELD(diffio);
      diff_try_update(&diffio, iOrig, invo);
   }

   // force updating cursor position on screen
   curPor->lastKnownCursor.lnum = 0;

theend:
   // A redraw is needed if there were diffs and they were cleared, or there
   // are diffs now, which means they got updated.
   if (curtab->first_diff || curtab->first_diff) {
      diff_redraw(true);
      applyAutocomms(EVENT_DIFFUPDATED, NULL, NULL, false, curBook);
   }
}

//Do a quick test if "diff" really works.  Otherwise it looks like there
//are no differences.  Can't use the return value, it's non-zero when
//there are differences.
private int
check_external_diff(DiffIo *diffio) {
   FILE* fd;
   int ok;
   int io_error = false;

   // May try twice, first with "-a" and then without.
   for (;;) {
      ok = false;
      fd = fopen((char *)diffio->orig.externalFname, "w");
      if (fd == NULL)
         io_error = true;
      else {
         if (fwrite("line1\n", (Unt)6, (Unt)1, fd) != 1)
            io_error = true;
         fclose(fd);
         fd = fopen((char *)diffio->new.externalFname, "w");
         if (!fd)
            io_error = true;
         else {
            if (fwrite("line2\n", (Unt)6, (Unt)1, fd) != 1)
               io_error = true;
            fclose(fd);
            fd = NULL;
            if (diff_file(diffio) == OK)
               fd = fopen((char *)diffio->dio_diff.outFname, "r");
            if (fd == NULL)
               io_error = true;
            else {
               Byte linebuf[LBUFLEN];

               for (;;) {
                  // For normal diff there must be a line that contains
                  // "1c1".  For unified diff "@@ -1 +1 @@".
                  if (eeFgets(linebuf, LBUFLEN, fd))
                     break;
                  if (STRNCMP(linebuf, "1c1", 3) == 0 || STRNCMP(linebuf, "@@ -1 +1 @@", 11) == 0)
                     ok = true;
               }
               fclose(fd);
            }
            mch_remove(diffio->dio_diff.outFname);
            mch_remove(diffio->new.externalFname);
         }
         mch_remove(diffio->orig.externalFname);
      }

      // When using @diffexpr, break here.
      if (p_dex)
         break;

      // If we checked if "-a" works already, break here.
      if (diff_a_works != MAYBE)
         break;
      diff_a_works = ok;

      // If "-a" works break here, otherwise retry without "-a".
      if (ok)
         break;
   }
   if (!ok) {
      if (io_error)
         emsg(_(e_cannot_read_or_write_temp_files));
      emsg(_(e_cannot_create_diffs));
      diff_a_works = MAYBE;
      return FAIL;
   }
   return OK;
}

//Invoke the xdiff function.
private int
diff_file_internal(DiffIo *diffio) {
   XpParam param;
   XdEmitConf emit_cfg;
   XdEmitCb emit_cb;

   CLEAR_FIELD(param);
   CLEAR_FIELD(emit_cfg);
   CLEAR_FIELD(emit_cb);

   param.flags = diff_algorithm;

   if ((diff_flags & DIFF_IWHITE) != 0)
      param.flags |= IGNORE_WHITESPACE_CHANGE;
   if ((diff_flags & DIFF_IWHITEALL) != 0)
      param.flags |= IGNORE_WHITESPACE;
   if ((diff_flags & DIFF_IWHITEEOL) != 0)
      param.flags |= IGNORE_WHITESPACE_AT_EOL;
   if ((diff_flags & DIFF_IBLANK) != 0)
      param.flags |= IGNORE_BLANK_LINES;

   emit_cfg.ctxlen = diffio->dio_ctxlen;
   emit_cb.priv = &diffio->dio_diff;
   if (diffio->dio_outfmt == DIO_OUTPUT_INDICES)
      emit_cfg.hunk_func = xdiff_out_indices;
   else
      emit_cb.out_line = xdiff_out_unified;
   if (xdl_diff(&diffio->orig.mmfile, &diffio->new.mmfile, &param, &emit_cfg, &emit_cb) < 0) {
      emsg(_(e_problem_creating_internal_diff));
      return FAIL;
   }
   return OK;
}

//Make a diff between files "tmp_orig" and "tmp_new", put the results into the "tmp_diff" file.
//Return OK or FAIL;
private int
diff_file(DiffIo* dio) {
   CS tmp_orig = dio->orig.externalFname;
   CS tmp_new = dio->new.externalFname;
   CS tmp_diff = dio->dio_diff.outFname;

   if (p_dex) {
      // Use @diffexpr to generate the diff file.
      eval_diff(tmp_orig, tmp_new, tmp_diff);
      return OK;
   } else
      // Use xdiff for generating the diff.
      if (dio->dio_internal)
         return diff_file_internal(dio);

   Unt lenOrig = STRLEN(tmp_orig);
   Unt lenNew = STRLEN(tmp_new);
   Unt lenDiff = STRLEN(tmp_diff);
   //6 is for `> 2>&1` which will turn into `>FILENAME 2>&1`; 27 is for
   Unt len = lenOrig + lenNew + STRLEN(tmp_diff) + 6 + 27;
   CS shellComm = alloc(len);

   //Build the diff command and execute it. Always use -a, binary differences are of no use. 
   //Ignore errors, diff returns non-zero when differences have been found.
   CS wr = shellComm;
   if (diff_a_works != 0) {
      memcpy(wr, "-a ", 3);
      wr += 3;
   } 
   if ((diff_flags & DIFF_IWHITE) != 0) {
      memcpy(wr, "-b", 3);
      wr += 3;
   } 
   if ((diff_flags & DIFF_IWHITEALL) != 0) {
      memcpy(wr, "-w", 3);
      wr += 3;
   } 
   if ((diff_flags & DIFF_IWHITEEOL) != 0) {
      memcpy(wr, "-Z", 3);
      wr += 3;
   } 
   if ((diff_flags & DIFF_IBLANK) != 0) {
      memcpy(wr, "-B", 3);
      wr += 3;
   } 
   if ((diff_flags & DIFF_ICASE) != 0) {
      memcpy(wr, "-i", 3);
      wr += 3;
   } 
   memcpy(wr, tmp_orig, lenOrig);
   wr += lenOrig;
   memcpy(wr, tmp_new, lenNew);
   wr += lenNew; 
   
   wr[0] = '>';
   wr++;
   memcpy(wr, tmp_diff, lenDiff);
   wr += lenDiff;
   memcpy(wr, " 2>&1", 5);
   wr += 5;
   wr[0] = ZERO;
   
   block_autocmds();   // avoid ShellCmdPost stuff
   
   (void)chCallShell(text(shellComm), SHELL_FILTER|SHELL_SILENT|SHELL_DOOUT);
   unblock_autocmds();
   
   eeglFree(shellComm);
   return OK;
}

//Create a new version of a file from the current book and a diff file.
//The book is written to a file, also for unmodified books (the file
//could have been produced by autocommands, e.g. the netrw plugin).
void
c_diffpatch(Invocation* invo) {
   Portal* old_curPor = curPor;
   CS newname = NULL;   // name of patched file book
   Byte dirbuf[MAXPATHL];
   FileStat st;

   // We need two temp file names.
   CS tmp_orig = eeTempName('o', false); // name of original temp file
   CS tmp_new = eeTempName('n', false); // name of patched temp file
   if (tmp_orig == NULL || tmp_new == NULL)
      goto theend;

   // Write the current book to "tmp_orig".
   if (bookWrite(curBook, tmp_orig, NULL,
         (LineNr)1, curBook->mem.lineCount, NULL, false, false, false, true) == FAIL)
      goto theend;

   // Get the absolute path of the patchfile, changing directory below.
   CS fullname = fiExpandAndCopy(invo->arg, false);
   CS esc_name = copyStr_shellescape(
          fullname != NULL ? fullname : invo->arg, true, true
   );
   Unt buflen = STRLEN(tmp_orig) + STRLEN(esc_name) + STRLEN(tmp_new) + 16;
   CS buf = alloc(buflen);

   //Temporarily chdir to /tmp, to avoid patching files in the current directory when the patch 
   //file contains more than one patch. When we have our own temp dir use that instead, it will 
   //be cleaned up when we exit (any .rej files created). Don't change directory if we can't
   //return to the current.
   if (mch_dirname(dirbuf, MAXPATHL) != OK || mch_chdir(dirbuf) != 0)
      dirbuf[0] = ZERO;
   else {
      if (eeTempDirG)
         (void)mch_chdir(eeTempDirG);
      else
         (void)mch_chdir(S"/tmp");
      shorten_fnames(true);
   }

   if (p_pex) {
      // Use 'patchexpr' to generate the new file.
      eval_patch(tmp_orig, fullname ? fullname : invo->arg, tmp_new);
   } else {
      //Build the patch command and execute it.  Ignore errors.  Switch to
      //cooked mode to allow the user to respond to prompts.
      eeSnprintf(buf, buflen, "patch -o %s %s < %s", tmp_new, tmp_orig, esc_name);
      block_autocmds();   // Avoid ShellCmdPost stuff
      
      (void)chCallShell(text(buf), SHELL_FILTER | SHELL_COOKED);
      unblock_autocmds();
   }

   if (dirbuf[0] != ZERO) {
      if (mch_chdir(dirbuf) != 0)
         emsg(_(e_cannot_go_back_to_previous_directory));
      shorten_fnames(true);
   }

   // patch probably has written over the screen
   redraw_later(UPD_CLEAR);

   // Delete any .orig or .rej file created.
   STRCPY(buf, tmp_new);
   STRCAT(buf, ".orig");
   mch_remove(buf);
   STRCPY(buf, tmp_new);
   STRCAT(buf, ".rej");
   mch_remove(buf);

   // Only continue if the output file was created.
   if (stat((char *)tmp_new, &st) < 0 || st.st_size == 0)
      emsg(_(e_cannot_read_patch_output));
   else {
      if (curBook->currFileName != NULL) {
         newname = copySubstr(curBook->currFileName, STRLEN(curBook->currFileName) + 4);
         if (newname)
            STRCAT(newname, ".new");
      }

      // don't use a new tab page, each tab page has its own diffs
      commModifierG.cmod_tab = 0;

      if (splitPortal(0, (diff_flags & DIFF_VERTICAL) != 0 ? WSP_VERT : 0) != FAIL) {
         // Pretend it was a ":split fname" command
         invo->id = C_split;
         invo->arg = tmp_new;
         do_exedit(invo, old_curPor);

         // check that split worked and editing tmp_new
         if (curPor != old_curPor && portalIsValid(old_curPor)) {
            // Set @diff' and 'wrap' off.
            diff_win_options(curPor, true);
            diff_win_options(old_curPor, true);

            if (newname) {
               // do a ":file filename.new" on the patched book
               invo->arg = newname;
               c_file(invo);

               // Do filetype detection with the new name.
               if (auGroupExists((CS)"filetypedetect"))
                  executeCommLine((CS)":doau filetypedetect BufRead");
            }
         }
      }
   }

theend:
   if (tmp_orig)
      mch_remove(tmp_orig);
   eeglFree(tmp_orig);
   if (tmp_new)
      mch_remove(tmp_new);
   eeglFree(tmp_new);
   eeglFree(newname);
   eeglFree(buf);
   eeglFree(fullname);
   eeglFree(esc_name);
}

//Split the portal and edit another file, setting options to show the diffs.
void
c_diffsplit(Invocation* invo) {
   Portal* old_curPor = curPor;
   BookRef old_curbuf;
   bookStoreInRef(OUT &old_curbuf, curBook);
   // Need to compute fraction when no redraw happened yet.
   validate_cursor();
   set_fraction(curPor);

   // don't use a new tab page, each tab page has its own diffs
   commModifierG.cmod_tab = 0;

   if (splitPortal(0, (diff_flags & DIFF_VERTICAL) != 0 ? WSP_VERT : 0) == FAIL)
      return;

   // Pretend it was a ":split fname" command
   invo->id = C_split;
   curPor->o.diff = true;
   do_exedit(invo, old_curPor);

   if (curPor == old_curPor)      // split didn't work
      return;

   // Set @diff and @wrap off.
   diff_win_options(curPor, true);
   if (portalIsValid(old_curPor)) {
      diff_win_options(old_curPor, true);
      if (bookRefValid(&old_curbuf))
         // Move the cursor position to that of the old portal.
         curPor->cursor.lnum = diff_get_corresponding_line( old_curbuf.c, old_curPor->cursor.lnum);
   }
   // Now that lines are folded scroll to show the cursor at the same relative position.
   scroll_to_fraction(curPor, curPor->height);
}

//Set options to show diffs for the current portal.
void
c_diffthis(Invocation* invo UNUSED) {
   // Set @diff' on and @wrap off.
   diff_win_options(curPor, true);
}

private void
set_diff_option(Portal* po, int value) {
   Portal* old_curPor = curPor;

   curPor = po;
   curBook = curPor->book;
   ++curBookLock;
   optChangeAndReportError(
      S"diff", (OptionValue){.tag = OPTION_NUM, .num = (long)value}, SET_LOCAL
   );
   --curBookLock;
   curPor = old_curPor;
   curBook = curPor->book;
}

//Set options in portal "po" for diff mode.
void
diff_win_options(Portal* po, int addbuf) {     // Add book to diff.
   Portal* old_curPor = curPor;

   // close the manually opened folds
   curPor = po;
   newFoldLevel();
   curPor = old_curPor;

   if ((diff_flags & DIFF_FOLLOWWRAP) == 0) {
      if (!po->o.diff)
         po->o.wrapSaved = po->o.wrap;
      po->o.wrap = false;
      po->skipCol = 0;
   }
   if (!po->o.diff) {
      po->o.foldMethodSaved = po->o.foldMethod;
   }
   optSetStringOptionDirectInPort(po, S"foldmethod", S"diff", OPT_LOCAL, 0);
   if (!po->o.diff) {
      po->o.foldEnableSave = po->o.foldEnable;
      po->o.foldLevelSaved = po->o.foldLevel;
   }
   po->o.foldEnable = true;
   po->o.foldLevel = 0;
   foldUpdateAll(po);
   // make sure topline is not halfway a fold
   didChangePortalSetting(po);
   if ((p_sbo & SCR_HOR) != 0)
      executeCommLine((CS)"set sbo+=hor");
   // Save the current values, to be restored in c_diffoff().
   po->o.diffSaved = true;

   set_diff_option(po, true);

   if (addbuf)
      diffAddBook(po->book);
   redrawPortLater(po, UPD_NOT_VALID);
}

//Set options not to show diffs. For the current portal or all portals. Only in the current tab.
void
c_diffoff(Invocation* invo) {
   int diffwin = false;

   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (invo->forceit ? po->o.diff : po == curPor) {
          // Set 'diff' off. If option values were saved in
          // diff_win_options(), restore the ones whose settings seem to have
          // been left over from diff mode.
          set_diff_option(po, false);

         if (po->o.diffSaved) {
            if ((diff_flags & DIFF_FOLLOWWRAP) == 0) {
               if (!po->o.wrap && po->o.wrapSaved) {
                  po->o.wrap = true;
                  po->leftCol = 0;
               }
            }
            po->o.foldMethod = po->o.foldMethodSaved;

            if (po->o.foldLevel == 0)
                po->o.foldLevel = po->o.foldLevelSaved;

            if (po->o.foldEnable)
               po->o.foldEnable = po->o.foldEnableSave;

            foldUpdateAll(po);
         }
         //remove filler lines
         po->topFill = 0;

         // make sure topline is not halfway a fold and cursor is invalidated
         didChangePortalSetting(po);

         // Note: 'sbo' is not restored, it's a global option.
         diffBookAdjust(po);
      }
      diffwin |= po->o.diff;
   }

   // Also remove hidden books from the list.
   if (invo->forceit)
      clearAllBooks();

   if (!diffwin) {
      needUpdateP = false;
      curtab->diff_invalid = false;
      curtab->diff_update = false;
      diff_clear(curtab);
   }

   // Remove "hor" from @scrollopt if there are no diff portals left.
   if (!diffwin && (p_sbo & SCR_HOR) != 0)
      executeCommLine(S"set scrollopt-=hor");
}

//Read the diff output and add each entry to the diff list.
private void
diff_read(
   int      iOrig, // idx of original file
   int      iNew,  // idx of new file
   DiffIo* dio      // diff output
){
   FILE* fd = NULL;
   int      line_hunk_idx = 0;  // line or hunk index
   DiffBlock* dprev = NULL;
   DiffBlock* dp = curtab->first_diff;
   DiffBlock* dn, *dpl;
   DiffResult* dout = &dio->dio_diff;
   Byte   linebuf[LBUFLEN];   // only need to hold the diff line
   Byte   *line;
   long   off;
   int      i;
   int      notset = true;       // block "*dp" not set yet
   Hunk   *hunk = NULL;       // init to avoid gcc warning

   enum {
      DIFF_ED,
      DIFF_UNIFIED,
      DIFF_NONE
   } diffstyle = DIFF_NONE;

   if (dout->outFname == NULL) {
      diffstyle = DIFF_UNIFIED;
   } else {
      fd = fopen((char *)dout->outFname, "r");
      if (fd == NULL) {
         emsg(_(e_cannot_read_diff_output));
         return;
      }
   }

   if (!dio->dio_internal) {
      hunk = ALLOC_ONE(Hunk);
   }

   for (;;) {
      if (dio->dio_internal) {
         if (line_hunk_idx >= dout->dout_ga.len)
            break;      // did last hunk
         hunk = ((Hunk **)dout->dout_ga.c)[line_hunk_idx++];
      } else {
         if (fd == NULL) {
            if (line_hunk_idx >= dout->dout_ga.len)
               break;       // did last line
            line = ((Byte **)dout->dout_ga.c)[line_hunk_idx++];
         } else {
            if (eeFgets(linebuf, LBUFLEN, fd))
                break;      // end of file
            line = linebuf;
         }

         if (diffstyle == DIFF_NONE) {
            // Determine diff style. ed like diff looks like this:
            // {first}[,{last}]c{first}[,{last}]
            // {first}a{first}[,{last}]
            // {first}[,{last}]d{first}
            //
            // unified diff looks like this:
            // --- file1       2018-03-20 13:23:35.783153140 +0100
            // +++ file2       2018-03-20 13:23:41.183156066 +0100
            // @@ -1,3 +1,5 @@
            if (SAFE_isdigit(*line))
                diffstyle = DIFF_ED;
            ei ((STRNCMP(line, "@@ ", 3) == 0))
                diffstyle = DIFF_UNIFIED;
            ei ((STRNCMP(line, "--- ", 4) == 0)
               && (eeFgets(linebuf, LBUFLEN, fd) == 0)
               && (STRNCMP(line, "+++ ", 4) == 0)
               && (eeFgets(linebuf, LBUFLEN, fd) == 0)
               && (STRNCMP(line, "@@ ", 3) == 0))
                diffstyle = DIFF_UNIFIED;
            else
                // Format not recognized yet, skip over this line.  Cygwin
                // diff may put a warning at the start of the file.
                continue;
         }

         if (diffstyle == DIFF_ED) {
            if (!SAFE_isdigit(*line))
               continue;   // not the start of a diff block
            if (parse_diff_ed(line, hunk) == FAIL)
               continue;
         } ei (diffstyle == DIFF_UNIFIED) {
            if (STRNCMP(line, "@@ ", 3)  != 0)
               continue;   // not the start of a diff block
            if (parse_diff_unified(line, hunk) == FAIL)
               continue;
         } else {
            emsg(_(e_invalid_diff_format));
            break;
         }
      }

      //Go over blocks before the change, for which orig and new are equal.
      //Copy blocks from orig to new.
      while (dp && hunk->origLnum > dp->lnum[iOrig] + dp->count[iOrig]) {
         if (notset)
            diff_copy_entry(dprev, dp, iOrig, iNew);
         dprev = dp;
         dp = dp->df_next;
         notset = true;
      }

      if (dp
         && hunk->origLnum <= dp->lnum[iOrig] + dp->count[iOrig]
         && hunk->origLnum + hunk->origCount >= dp->lnum[iOrig]
      ) {
         // New block overlaps with existing block(s).
         // First find last block that overlaps.
         for (dpl = dp; dpl->df_next != NULL; dpl = dpl->df_next)
            if (hunk->origLnum + hunk->origCount < dpl->df_next->lnum[iOrig])
                break;

         // If the newly found block starts before the old one, set the
         // start back a number of lines.
         off = dp->lnum[iOrig] - hunk->origLnum;
         if (off > 0) {
            for (i = iOrig; i < iNew; ++i) {
               if (curtab->diffbuf[i] != NULL) {
                  dp->lnum[i] -= off;
                  dp->count[i] += off;
               }
            } 
            dp->lnum[iNew] = hunk->newLnum;
            dp->count[iNew] = hunk->newCount;
         } ei (notset) {
            // new block inside existing one, adjust new block
            dp->lnum[iNew] = hunk->newLnum + off;
            dp->count[iNew] = hunk->newCount - off;
         } else {
            // second overlap of new block with existing block

            //if this hunk has different orig/new counts, adjust
            //the diff block size first. When we handled the first hunk we
            //would have expanded it to fit, without knowing that this hunk exists
            int orig_size_in_dp = MIN(hunk->origCount,
               dp->lnum[iOrig] + dp->count[iOrig] - hunk->origLnum
            );
            int size_diff = hunk->newCount - orig_size_in_dp;
            dp->count[iNew] += size_diff;

            // grow existing block to include the overlap completely
            off = hunk->newLnum + hunk->newCount - (dp->lnum[iNew] + dp->count[iNew]);
            if (off > 0)
                dp->count[iNew] += off;
         }

         // Adjust the size of the block to include all the lines to the
         // end of the existing block or the new diff, whatever ends last.
         off = (hunk->origLnum + hunk->origCount) - (dpl->lnum[iOrig] + dpl->count[iOrig]);
         if (off < 0) {
            //new change ends in existing block, adjust the end. We only
            //need to do this once per block or we will over-adjust.
            if (notset || dp != dpl) {
               //adjusting by 'off' here is only correct if there is not another hunk in this block. we
               //adjust for this when we encounter a second overlap later.
               dp->count[iNew] += -off;
            }
            off = 0;
         }
         for (i = iOrig; i < iNew; ++i) {
            if (curtab->diffbuf[i] != NULL)
                dp->count[i] = dpl->lnum[i] + dpl->count[i] - dp->lnum[i] + off;
         } 

         // Delete the diff blocks that have been merged into one.
         dn = dp->df_next;
         dp->df_next = dpl->df_next;
         while (dn != dp->df_next) {
            dpl = dn->df_next;
            clear_diffblock(dn);
            dn = dpl;
         }
      } else {
         // Allocate a new diffblock.
         dp = diff_alloc_new(curtab, dprev, dp);
         if (!dp)
            goto done;

         dp->lnum[iOrig] = hunk->origLnum;
         dp->count[iOrig] = hunk->origCount;
         dp->lnum[iNew] = hunk->newLnum;
         dp->count[iNew] = hunk->newCount;

         // Set values for other books, these must be equal to the
         // original buffer, otherwise there would have been a change already.
         for (i = iOrig + 1; i < iNew; ++i) {
            if (curtab->diffbuf[i] != NULL)
                diff_copy_entry(dprev, dp, iOrig, i);
         } 
      }
      notset = false;      // "*dp" has been set
   }

   // for remaining diff blocks orig and new are equal
   while (dp) {
      if (notset)
         diff_copy_entry(dprev, dp, iOrig, iNew);
      dprev = dp;
      dp = dp->df_next;
      notset = true;
   }

done:
   if (!dio->dio_internal)
      eeglFree(hunk);

   if (fd)
      fclose(fd);
}

//Copy an entry at "dp" from "iOrig" to "iNew".
private void
diff_copy_entry(DiffBlock* prev, DiffBlock* dp, int iOrig, int iNew) {
   Long off = prev 
      ? (prev->lnum[iOrig] + prev->count[iOrig]) - (prev->lnum[iNew] + prev->count[iNew]) : 0;
      
   dp->lnum[iNew] = dp->lnum[iOrig] - off;
   dp->count[iNew] = dp->count[iOrig];
}

//Clear the list of diffblocks for tab "t".
void
diff_clear(Tab* t) {
   DiffBlock* next_p;
   for (DiffBlock* p = t->first_diff; p; p = next_p) {
      next_p = p->df_next;
      clear_diffblock(p);
   }
   t->first_diff = NULL;
}

// return true if the options are set to use diff linematch
private int
diff_linematch(DiffBlock *dp) {
   if ((diff_flags & DIFF_LINEMATCH) == 0)
      return 0;

   // are there more than three diff books?
   int tsize = 0;
   for (Unt i = 0; i < DB_COUNT; i++) {
      if (curtab->diffbuf[i] != NULL) {
         //for the rare case (bug?) that the count of a diff block is negative, do not run the 
         //algorithm because this will try to allocate a negative amount of space and crash
         if (dp->count[i] < 0)
            return false;
         tsize += dp->count[i];
      }
   }

   // avoid allocating a huge array because it will lag
   return tsize <= linematch_lines;
}

private int
get_max_diff_length(const DiffBlock* dp) {
   int maxlength = 0;

   for (Unt k = 0; k < DB_COUNT; k++) {
      if (curtab->diffbuf[k] && dp->count[k] > maxlength)
         maxlength = dp->count[k];
   }
   return maxlength;
}

//Find the first diff block that includes the specified line. Also find the
//next diff block that's not in the current chain of adjacent blocks that are
//all touching each other directly.
private void
find_top_diff_block(
   OUT DiffBlock** thistopdiff,
   OUT DiffBlock** next_adjacent_blocks,
   int fromidx,
   int topline
) {
   DiffBlock* topdiff = NULL;
   DiffBlock* localtopdiff = NULL;
   int topdiffchange = 0;

   for (topdiff = curtab->first_diff; topdiff != NULL; topdiff = topdiff->df_next) {
      // set the top of the current overlapping diff block set as we
      // iterate through all of the sets of overlapping diff blocks
      if (!localtopdiff || topdiffchange) {
         localtopdiff = topdiff;
         topdiffchange = 0;
      }

      // check if the fromwin topline is matched by the current diff. if so,
      // set it to the top of the diff block
      if (topline >= topdiff->lnum[fromidx] && topline <=
         (topdiff->lnum[fromidx] + topdiff->count[fromidx]))
      {
         // this line is inside the current diff block, so we will save the
         // top block of the set of blocks to refer to later
         if ((*thistopdiff) == NULL)
            (*thistopdiff) = localtopdiff;
      }

      // check if the next set of overlapping diff blocks is next
      if (!(topdiff->df_next && (topdiff->df_next->lnum[fromidx] ==
            (topdiff->lnum[fromidx] + topdiff->count[fromidx])))
      ) {
         // mark that the next diff block is belongs to a different set of overlapping diff blocks
         topdiffchange = 1;

         // if we already have found that the line number is inside a diff
         // block, set the marker of the next block and finish the iteration
         if (*thistopdiff) {
            (*next_adjacent_blocks) = topdiff->df_next;
            break;
         }
      }
   }
}

//Calculates topline/topfill of a target diff portal to fit the source diff portal.
private void
calculate_topfill_and_topline(
   int const fromidx,
   int const toidx,
   int const from_topline,
   int const from_topfill,
   OUT int* topfill,
   OUT LineNr* topline
) {
   //find the position from the top of the diff block, and the next diff block that's no longer 
   //adjacent to the current block. "Adjacency" means a chain of diff blocks that are directly 
   //touching each other, allowed by linematch and diff anchors.
   DiffBlock* thistopdiff = NULL;
   DiffBlock* next_adjacent_blocks = NULL;
   int virtual_lines_passed = 0;

   find_top_diff_block(OUT &thistopdiff, OUT &next_adjacent_blocks, fromidx, from_topline);

   // count the virtual lines (either filler or concrete line) that have been
   // passed in the source book. There could be multiple diff blocks if
   // there are adjacent empty blocks (count == 0 at fromidx).
   DiffBlock *curdif = thistopdiff;
   while (curdif && (curdif->lnum[fromidx] + curdif->count[fromidx]) <= from_topline) {
      virtual_lines_passed += get_max_diff_length(curdif);
      curdif = curdif->df_next;
   }

   if (curdif != next_adjacent_blocks)
      virtual_lines_passed += from_topline - curdif->lnum[fromidx];
   virtual_lines_passed -= from_topfill;

   //clamp negative values in case from_topfill hasn't been updated yet and
   //is larger than total virtual lines, which could happen when setting
   //diffopt multiple times
   if (virtual_lines_passed < 0)
      virtual_lines_passed = 0;

   //move the same amount of virtual lines in the target book to find the cursor's line number
   int curlinenum_to = thistopdiff->lnum[toidx];

   int virt_lines_left = virtual_lines_passed;
   curdif = thistopdiff;
   while (virt_lines_left > 0 && curdif != NULL && curdif != next_adjacent_blocks) {
      curlinenum_to += MIN(virt_lines_left, curdif->count[toidx]);
      virt_lines_left -= MIN(virt_lines_left, get_max_diff_length(curdif));
      curdif = curdif->df_next;
   }

   //count the total number of virtual lines between the top diff block and
   //the found line in the target book
   int max_virt_lines = 0;
   for (DiffBlock *dp = thistopdiff; dp != NULL; dp = dp->df_next) {
      if (dp->lnum[toidx] + dp->count[toidx] <= curlinenum_to)
          max_virt_lines += get_max_diff_length(dp);
      else {
         if (dp->lnum[toidx] <= curlinenum_to)
            max_virt_lines += curlinenum_to - dp->lnum[toidx];
         break;
      }
   }

   if ((diff_flags & DIFF_FILLER) != 0)
      // should always be non-negative as max_virt_lines is larger
      (*topfill) = max_virt_lines - virtual_lines_passed;
   (*topline) = curlinenum_to;
}

//Apply results from the linematch algorithm and apply to 'dp' by splitting it
//into multiple adjacent diff blocks.
private void
apply_linematch_results(DiffBlock* dp, Unt decisions_length, int* decisions) {
   //get the start line number here in each diff book, and then increment
   int line_numbers[DB_COUNT];
   int outputmap[DB_COUNT];
   Unt ndiffs = 0;

   for (Unt i = 0; i < DB_COUNT; i++) {
      if (curtab->diffbuf[i] != NULL) {
         line_numbers[i] = dp->lnum[i];
         dp->count[i] = 0;

         //Keep track of the index of the diff book we are using here. We will use this to write 
         //the output of the algorithm to DiffBlock structs at the correct indexes
         outputmap[ndiffs] = i;
         ndiffs++;
      }
   }

   // write the diffs starting with the current diff block
   DiffBlock *dp_s = dp;
   for (Unt i = 0; i < decisions_length; i++) {
      // Don't allocate on first iter since we can reuse the initial diffblock
      if (i != 0 && (decisions[i - 1] != decisions[i])) {
         // create new sub diff blocks to segment the original diff block
         // which we further divided by running the linematch algorithm
         dp_s = diff_alloc_new(curtab, dp_s, dp_s->df_next);
         dp_s->isLinematched = true;
         for (Unt j = 0; j < DB_COUNT; j++) {
            if (curtab->diffbuf[j] != NULL) {
               dp_s->lnum[j] = line_numbers[j];
               dp_s->count[j] = 0;
            }
         }
      }
      for (Unt j = 0; j < ndiffs; j++) {
         if (decisions[i] & (1 << j)) {
            // will need to use the map here
            dp_s->count[outputmap[j]]++;
            line_numbers[outputmap[j]]++;
         }
      }
   }
   dp->isLinematched = true;
}

private void
run_linematch_algorithm(DiffBlock* dp) {
   // define buffers for diff algorithm
   DiffInp diffbufs_mm[DB_COUNT];
   MmFile const* diffbufs[DB_COUNT];
   int diff_length[DB_COUNT];
   Unt ndiffs = 0;

   for (Unt i = 0; i < DB_COUNT; i++) {
      if (curtab->diffbuf[i] != NULL) {
         // write the contents of the entire buffer to diffbufs_mm[diffbuffers_count]
         if (dp->count[i] > 0) {
            diff_write_buffer(curtab->diffbuf[i], &diffbufs_mm[ndiffs],
               dp->lnum[i], dp->lnum[i] + dp->count[i] - 1);
         } else {
            diffbufs_mm[ndiffs].mmfile.size = 0;
            diffbufs_mm[ndiffs].mmfile.ptr = NULL;
         }

         diffbufs[ndiffs] = &diffbufs_mm[ndiffs].mmfile;

         // keep track of the length of this diff block to pass it to the linematch algorithm
         diff_length[ndiffs] = dp->count[i];

         // increment the amount of diff buffers we are passing to the algorithm
         ndiffs++;
      }
   }

   // we will get the output of the linematch algorithm in the format of an
   // array of integers (*decisions) and the length of that array (decisions_length)
   int *decisions = NULL;
   const int iwhite = (diff_flags & (DIFF_IWHITEALL | DIFF_IWHITE)) > 0 ? 1 : 0;
   Unt decisions_length = linematch_nbuffers(diffbufs, diff_length, ndiffs, &decisions, iwhite);

   for (Unt i = 0; i < ndiffs; i++)
      free(diffbufs_mm[i].mmfile.ptr); // TODO should this be eeglFree ?

   apply_linematch_results(dp, decisions_length, decisions);

   free(decisions);
}

//Check diff status for line "lnum" in book "book":
//Return > 0 for inserting that many filler lines above it (never happens
//when @diffopt doesn't contain "filler"). Otherwise return 0.
//
//"linestatus" (can be NULL) will be set to:
//0 for nothing special.
//-1 for a line that should be highlighted as changed.
//-2 for a line that should be highlighted as added/deleted.
//
//This should only be used for portals where @diff is set.
//
//Note that it's possible for a changed/added/deleted line to also have filler
//lines above it. This happens when using linematch or using diff anchors (at the anchored lines).
int
diff_check_with_linestatus(Portal *po, LineNr lnum, OUT LineDiffStatus* linestatus) {
   DiffBlock   *dp;
   int      maxcount;
   Book* book = po->book;
   int      cmp;

   if (linestatus)
      *linestatus = LINE_STATUS_UNCHANGED;

   if (curtab->diff_invalid)
      c_diffupdate(NULL);      // update after a big change

   if (curtab->first_diff == NULL || !po->o.diff)   // no diffs at all
      return 0;

   //safety check: "lnum" must be a book line
   if (lnum < 1 || lnum > book->mem.lineCount + 1)
      return 0;

   Unt idx = bookIndex(book);      // index in diffbuf[] for this book
   if (idx == UNT)
      return 0;      // no diffs for book "book"

   //A closed fold never has filler lines.
   if (getFoldsPortal(po, lnum, NULL, NULL, true, NULL))
      return 0;

   //search for a change that includes "lnum" in the list of diffblocks.
   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (lnum <= dp->lnum[idx] + dp->count[idx])
         break;
   } 
   if (dp == NULL || lnum < dp->lnum[idx])
      return 0;

   //Don't run linematch when lnum is offscreen.  Useful for scrollbind
   //calculations which need to count all the filler lines above the screen.
   if (lnum >= po->topLine && lnum < po->bottomLine
            && !dp->isLinematched && diff_linematch(dp)
            && checkSanity(curtab, dp))
      run_linematch_algorithm(dp);

   //Insert filler lines above the line just below the change.  Will return 0
   //when this buf had the max count.
   int num_fill = 0;
   while (lnum == dp->lnum[idx] + dp->count[idx]) {
      // Only calculate fill lines if 'diffopt' contains "filler". Otherwise return 0 filler lines
      if ((diff_flags & DIFF_FILLER) != 0) {
         maxcount = get_max_diff_length(dp);
         num_fill += maxcount - dp->count[idx];
      }

      //If there are adjacent blocks (e.g. linematch or anchor), loop through them. It's possible 
      //for multiple adjacent blocks to contribute to filler lines.
      //This also helps us find the last diff block in the list of adjacent
      //blocks which is necessary when it is a change/inserted line right after added lines.
      if (dp->df_next != NULL
            && lnum >= dp->df_next->lnum[idx]
            && lnum <= dp->df_next->lnum[idx] + dp->df_next->count[idx])
         dp = dp->df_next;
      else
         break;
   }

   if (lnum < dp->lnum[idx] + dp->count[idx]) {
      int zero = false;

      //Changed or inserted line. If the other books have a count of 0, the lines were 
      //inserted. If the other books have the same count, check if the lines are identical
      cmp = false;
      for (Unt i = 0; i < DB_COUNT; ++i) {
         if (i != idx && curtab->diffbuf[i] != NULL) {
            if (dp->count[i] == 0)
               zero = true;
            else {
               if (dp->count[i] != dp->count[idx]) {
                  if (linestatus)
                     *linestatus = LINE_STATUS_CHANGED;   // nr of lines changed.
                  return num_fill;
               }
               cmp = true;
            }
         }
      } 
      if (cmp) {
         //Compare all lines.  If they are equal the lines were inserted
         //in some books, deleted in others, but not changed.
         for (Unt i = 0; i < DB_COUNT; ++i) {
            if ((i != idx && curtab->diffbuf[i] != NULL && dp->count[i] != 0)
               && (!diff_equal_entry(dp, idx, i))
            ) {
               if (linestatus)
                  *linestatus = LINE_STATUS_CHANGED;
               return num_fill;
            }
         }
      }
      //If there is no book with zero lines then there is no difference any longer. Happens when 
      //making a change (or undo) that removes the difference. Can't remove the entry here, we 
      //might be halfway updating the portal. Just report the text as unchanged. Other
      //portals might still show the change though.
      if (zero == false)
         return num_fill;
      if (linestatus)
         *linestatus = LINE_STATUS_ADDED_OR_DELETED;
   } 
   return num_fill;
}

// Compare two entries in diff "*dp" and return true if they are equal.
private int
diff_equal_entry(DiffBlock *dp, Unt idx1, Unt idx2) {
   if (dp->count[idx1] != dp->count[idx2])
      return false;
   if (checkSanity(curtab, dp) == FAIL)
      return false;
   for (int i = 0; i < dp->count[idx1]; ++i) {
      CS line = copyStr(memGetLine(curtab->diffbuf[idx1], dp->lnum[idx1] + i, false));
      int cmp = diff_cmp(line, memGetLine(curtab->diffbuf[idx2], dp->lnum[idx2] + i, false));
      eeglFree(line);
      if (cmp != 0)
         return false;
    }
    return true;
}

//Compare the characters at "p1" and "p2".  If they are equal (possibly
//ignoring case) return true and set "len" to the number of bytes.
private int
diff_equal_char(CS p1, CS p2, OUT int* len) {
   Unt l  = utfCharLen(p1);

   if (l != utfCharLen(p2))
      return false;
   if (l > 1) {
      if (STRNCMP(p1, p2, l) != 0
         && ((diff_flags & DIFF_ICASE) == 0
             || utf_fold(mb_ptr2char(p1)) != utf_fold(mb_ptr2char(p2)))) {
          return false;
      } 
      *len = l;
   } else {
      if ((*p1 != *p2) && ((diff_flags & DIFF_ICASE) == 0 || TOLOWER_LOC(*p1) != TOLOWER_LOC(*p2)))
         return false;
      *len = 1;
   }
   return true;
}

//Compare strings "s1" and "s2" according to 'diffopt'. Return non-zero when they are different.
private int
diff_cmp(CS s1, CS s2){
   if ((diff_flags & DIFF_IBLANK) != 0 && (*skipwhite(s1) == ZERO || *skipwhite(s2) == ZERO))
      return 0;

   if ((diff_flags & (DIFF_ICASE | ALL_WHITE_DIFF)) == 0)
      return STRCMP(s1, s2);
   if ((diff_flags & DIFF_ICASE) != 0 && (diff_flags & ALL_WHITE_DIFF) == 0)
      return caseInsensitiveCompareMaxCol(s1, s2);

   CS p1 = s1;
   CS p2 = s2;

   // Ignore white space changes and possibly ignore case.
   while (*p1 != ZERO && *p2 != ZERO) {
      if (((diff_flags & DIFF_IWHITE) && SPACE_OR_TAB(*p1) && SPACE_OR_TAB(*p2))
         || ((diff_flags & DIFF_IWHITEALL) && (SPACE_OR_TAB(*p1) || SPACE_OR_TAB(*p2)))
      ) {
         p1 = skipwhite(p1);
         p2 = skipwhite(p2);
      } else {
         int l;
         if (!diff_equal_char(p1, p2, OUT &l))
            break;
         p1 += l;
         p2 += l;
      }
   }

   // Ignore trailing white space.
   p1 = skipwhite(p1);
   p2 = skipwhite(p2);
   if (*p1 != ZERO || *p2 != ZERO)
      return 1;
   return 0;
}

// Return the number of filler lines above "lnum".
int
diff_check_fill(Portal* po, LineNr lnum){
   // be quick when there are no filler lines
   if ((diff_flags & DIFF_FILLER) == 0)
      return 0;
   int n = diff_check_with_linestatus(po, lnum, NULL);
   if (n <= 0)
      return 0;
   return n;
}

//Set the topline of "towin" to match the position in "fromwin", so that they
//show the same diff'ed lines.
void
diff_set_topline(Portal* fromPort, Portal* toPort){
   Book* fromBook = fromPort->book;
   LineNr   lnum = fromPort->topLine;
   Unt      toidx;
   DiffBlock   *dp;

   Unt fromidx = bookIndex(fromBook);
   if (fromidx == UNT)
      return;      // safety check

   if (curtab->diff_invalid)
      c_diffupdate(NULL);      // update after a big change

   toPort->topFill = 0;

   // search for a change that includes "lnum" in the list of diffblocks.
   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (lnum <= dp->lnum[fromidx] + dp->count[fromidx])
         break;
   } 
   if (!dp) {
      // After last change, compute topline relative to end of file; no filler lines.
      toPort->topLine = toPort->book->mem.lineCount - (fromBook->mem.lineCount - lnum);
   } else {
      // Find index for "towin".
      toidx = bookIndex(toPort->book);
      if (toidx == UNT)
         return;      // safety check

      toPort->topLine = lnum + (dp->lnum[toidx] - dp->lnum[fromidx]);
      if (lnum >= dp->lnum[fromidx]) {
         calculate_topfill_and_topline(
               fromidx, 
               toidx,
               fromPort->topLine,
               fromPort->topFill,
               OUT &toPort->topFill,
               OUT &toPort->topLine
         );
      }
   }

   // safety check (if diff info gets outdated strange things may happen)
   toPort->bottFill = false;
   if (toPort->topLine > toPort->book->mem.lineCount) {
      toPort->topLine = toPort->book->mem.lineCount;
      toPort->bottFill = true;
   }
   if (toPort->topLine < 1) {
      toPort->topLine = 1;
      toPort->topFill = 0;
   }

   // When topLine changes need to recompute bottomLine and cursor position
   invalidate_botline_win(toPort);
   changed_line_abv_curs_win(toPort);

   check_topfill(toPort, false);
   (void)getFoldsPortal(toPort, toPort->topLine, OUT &toPort->topLine, NULL, true, NULL);
}

private int
parse_diffanchors(
   NULLABLE CS diffAnchors,
   Boole check_only, //will only make sure the syntax is correct.
   Book* book,
   LineNr* anchors,
   OUT Unt* countAnchors
) {
   if (!diffAnchors)
      return OK;
   CS dia = diffAnchors;

   Book* origCurBook = curBook;
   Portal* orig_curPor = curPor;

   Portal* bookPort = NULL;
   if (check_only)
      bookPort = curPor;
   else {
      // Find the first portal tied to this book and ignore the rest. Will
      // only matter for portal-specific addresses like `.` or `''`.
      FOR_ALL_PORTALS(bookPort) {
         if (bookPort->book == book && bookPort->o.diff)
            break;
      } 
      if (!bookPort && dia) {
         //The book is hidden. Currently this is not supported due to the edge cases of needing
         //to decide if an address is portal-specific or not. We could add more checks in the 
         //future so we can detect whether an address relies on curPor to make this more fleixble.
         emsg(_(e_diff_anchors_with_hidden_windows));
         return FAIL;
      }
   }

   Unt i;
   for (i = 0; i < MAX_DIFF_ANCHORS && *dia != ZERO; i++) {
      if (*dia == ',') // don't allow empty values
         return FAIL;

      curBook = book;
      curPor = bookPort;
      LineNr lnum = doGetCommandAddress(NULL, OUT &dia, ADDR_LINES, check_only, true, false, 1);
      curBook = origCurBook;
      curPor = orig_curPor;

      if (!dia || (*dia != ',' && *dia != ZERO)) // error detected
         return FAIL;

      if (!check_only && (lnum == MAXLNUM || lnum <= 0 || lnum > book->mem.lineCount + 1)) {
         emsg(_(e_invalid_range));
         return FAIL;
      }

      if (anchors)
         anchors[i] = lnum;

      if (*dia == ',')
         dia++;
   }
   if (i == MAX_DIFF_ANCHORS && *dia != ZERO) {
      showErrFmtMsg(_(e_cannot_have_more_than_nr_diff_anchors), MAX_DIFF_ANCHORS);
      return FAIL;
   }
   if (countAnchors)
      *countAnchors = i;
   return OK;
}

// This is called when @diffanchors is changed.
int
diffanchors_changed(CS newVal, Boole buflocal) {
   int diffResult = parse_diffanchors(newVal, true, curBook, NULL, NULL);
   if (diffResult == OK && (diff_flags & DIFF_ANCHOR) != 0) {
      Tab   *t;
      FOR_ALL_TABS(t) {
         if (!buflocal)
            t->diff_invalid = true;
         else {
            for (Unt idx = 0; idx < DB_COUNT; ++idx) {
               if (t->diffbuf[idx] == curBook) {
                  t->diff_invalid = true;
                  break;
               }
            } 
         }
      }
   }
   return diffResult;
}

// This is called when @diffopt is changed.
int
diffopt_changed(CS newVal) {
   int diff_context_new = 6;
   int linematch_lines_new = 0;
   Unt diff_flags_new = 0;
   long diff_algorithm_new = 0;
   long diff_indent_heuristic = 0;
   if (!newVal)
      goto finishedParsing;
      
   CS p = newVal;
   Tab* t;
   while (*p != ZERO) {
      // Note: Keep this in sync with option.c:p_dip_values
      if (STRNCMP(p, "filler", 6) == 0) {
         p += 6;
         diff_flags_new |= DIFF_FILLER;
      } ei (STRNCMP(p, "anchor", 6) == 0) {
         p += 6;
         diff_flags_new |= DIFF_ANCHOR;
      } ei (STRNCMP(p, "context:", 8) == 0 && EE_ISDIGIT(p[8])) {
         p += 8;
         diff_context_new = parseLong(&p);
      } ei (STRNCMP(p, "iblank", 6) == 0) {
         p += 6;
         diff_flags_new |= DIFF_IBLANK;
      } ei (STRNCMP(p, "icase", 5) == 0) {
         p += 5;
         diff_flags_new |= DIFF_ICASE;
      } ei (STRNCMP(p, "iwhiteall", 9) == 0) {
         p += 9;
         diff_flags_new |= DIFF_IWHITEALL;
      } ei (STRNCMP(p, "iwhiteeol", 9) == 0) {
         p += 9;
         diff_flags_new |= DIFF_IWHITEEOL;
      } ei (STRNCMP(p, "iwhite", 6) == 0) {
         p += 6;
         diff_flags_new |= DIFF_IWHITE;
      } ei (STRNCMP(p, "horizontal", 10) == 0) {
         p += 10;
         diff_flags_new |= DIFF_HORIZONTAL;
      } ei (STRNCMP(p, "vertical", 8) == 0) {
         p += 8;
         diff_flags_new |= DIFF_VERTICAL;
      } ei (STRNCMP(p, "hiddenoff", 9) == 0) {
         p += 9;
          diff_flags_new |= DIFF_HIDDEN_OFF;
      } ei (STRNCMP(p, "closeoff", 8) == 0) {
          p += 8;
          diff_flags_new |= DIFF_CLOSE_OFF;
      } ei (STRNCMP(p, "followwrap", 10) == 0) {
          p += 10;
          diff_flags_new |= DIFF_FOLLOWWRAP;
      } ei (STRNCMP(p, "indent-heuristic", 16) == 0) {
          p += 16;
          diff_indent_heuristic = INDENT_HEURISTIC;
      } ei (STRNCMP(p, "internal", 8) == 0) {
          p += 8;
          diff_flags_new |= DIFF_INTERNAL;
      } ei (STRNCMP(p, "algorithm:", 10) == 0) {
         // Note: Keep this in sync with p_dip_algorithm_values.
         p += 10;
         if (STRNCMP(p, "myers", 5) == 0) {
            p += 5;
            diff_algorithm_new = 0;
         } ei (STRNCMP(p, "minimal", 7) == 0) {
            p += 7;
            diff_algorithm_new = NEED_MINIMAL;
         } ei (STRNCMP(p, "patience", 8) == 0) {
            p += 8;
            diff_algorithm_new = PATIENCE_DIFF;
         } ei (STRNCMP(p, "histogram", 9) == 0) {
            p += 9;
            diff_algorithm_new = XDF_HISTOGRAM_DIFF;
         } else
            return FAIL;
      } ei (STRNCMP(p, "inline:", 7) == 0) {
          // Note: Keep this in sync with p_dip_inline_values.
          p += 7;
          if (STRNCMP(p, "none", 4) == 0) {
            p += 4;
            diff_flags_new &= ~(ALL_INLINE);
            diff_flags_new |= DIFF_INLINE_NONE;
          } ei (STRNCMP(p, "simple", 6) == 0) {
            p += 6;
            diff_flags_new &= ~(ALL_INLINE);
            diff_flags_new |= DIFF_INLINE_SIMPLE;
         } ei (STRNCMP(p, "char", 4) == 0) {
            p += 4;
            diff_flags_new &= ~(ALL_INLINE);
            diff_flags_new |= DIFF_INLINE_CHAR;
         } ei (STRNCMP(p, "word", 4) == 0) {
            p += 4;
            diff_flags_new &= ~(ALL_INLINE);
            diff_flags_new |= DIFF_INLINE_WORD;
         } else
            return FAIL;
      } ei (STRNCMP(p, "linematch:", 10) == 0 && EE_ISDIGIT(p[10])) {
         p += 10;
         linematch_lines_new = parseLong(&p);
         diff_flags_new |= DIFF_LINEMATCH;

         // linematch does not make sense without filler set
         diff_flags_new |= DIFF_FILLER;
      }

      if (*p != ',' && *p != ZERO)
         return FAIL;
      if (*p == ',')
          ++p;
   }
finishedParsing: 

   diff_algorithm_new |= diff_indent_heuristic;

   // Can't have both "horizontal" and "vertical".
   if ((diff_flags_new & DIFF_HORIZONTAL) && (diff_flags_new & DIFF_VERTICAL))
      return FAIL;

   // If flags were added or removed, or the algorithm was changed, need to update the diff.
   if (diff_flags != diff_flags_new || diff_algorithm != diff_algorithm_new) {
      FOR_ALL_TABS(t) {
         t->diff_invalid = true;
      } 
   } 

   diff_flags = diff_flags_new;
   diff_context = diff_context_new == 0 ? 1 : diff_context_new;
   linematch_lines = linematch_lines_new;
   diff_algorithm = diff_algorithm_new;

   diff_redraw(true);

   // recompute the scroll binding with the new option value, may
   // remove or add filler lines
   check_scrollbind((LineNr)0, 0L);
   p_dip = newVal;
   return OK;
}

//Return true if 'diffopt' contains "horizontal".
int
diffopt_horizontal(void) {
   return (diff_flags & DIFF_HORIZONTAL) != 0;
}

//Return true if 'diffopt' contains "hiddenoff".
int
diffopt_hiddenoff(void) {
   return (diff_flags & DIFF_HIDDEN_OFF) != 0;
}

//Return true if 'diffopt' contains "closeoff".
int
diffopt_closeoff(void) {
   return (diff_flags & DIFF_CLOSE_OFF) != 0;
}

//Called when a line has been updated. Used for updating inline diff in Insert
//mode without waiting for global diff update later.
void
diff_update_line(LineNr lnum) {
   if ((diff_flags & ALL_INLINE_DIFF) == 0)
      // We only care if we are doing inline-diff where we cache the diff results
      return;

   Unt idx = bookIndex(curBook);
   if (idx == UNT)
      return;
      
   DiffBlock* dp;
   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (lnum <= dp->lnum[idx] + dp->count[idx])
          break;
   } 

   // clear the inline change cache as it's invalid
   if (dp != NULL) {
      dp->has_changes = false;
      dp->changes.len = 0;
   }
}

private DifflineChange simple_diffline_change; // used for simple inline diff algorithm

// Parse a diffline struct and return the [start,end] byte offsets
// Return true if this change was added, no other book has it.
int
diff_change_parse(DiffLine* diffline, DifflineChange* change, int* change_start, int* change_end) {
   if (change->dc_start_lnum_off[diffline->bufidx] < diffline->lineoff)
      *change_start = 0;
   else
      *change_start = change->dc_start[diffline->bufidx];
   if (change->dc_end_lnum_off[diffline->bufidx] > diffline->lineoff)
      *change_end = INT_MAX;
   else
      *change_end = change->dc_end[diffline->bufidx];

   if (change == &simple_diffline_change) {
      //This is what we returned from simple inline diff. We always consider
      //the range to be changed, rather than added for now.
      return false;
   }

   //Find out whether this is an addition. Note that for multi book diff,
   //to tell whether lines are additions we check whether all the other diff
   //lines are identical (in diff_check_with_linestatus). If so, we mark them
   //as add. We don't do that for inline diff here for simplicity.
   for (Unt i = 0; i < DB_COUNT; i++) {
      if (i == (Unt)diffline->bufidx)
         continue;
      if (change->dc_start[i] != change->dc_end[i]
         || change->dc_end_lnum_off[i] != change->dc_start_lnum_off[i]
      ){
          return false;
      }
   }
   return true;
}

//Find the difference within a changed line and return [startp,endp] byte
//positions. Perform a simple algorithm by finding a single range in the middle.
//
//If diffopt has DIFF_INLINE_NONE set, then this will only calculate the return
//value (added or changed), but startp/endp will not be calculated.
//
//Return true if the line was added, no other book has it.
private int
diff_find_change_simple(
   Portal* po,
   LineNr lnum,
   DiffBlock* dp,
   Unt idx,
   OUT int* startp,   // first char of the change
   OUT int* endp      // last char of the change
){
   int si_org, si_new;
   int ei_org, ei_new;
   int added = true;
   CS p1;
   CS p2;
   int l;

   CS line_org = ((diff_flags & DIFF_INLINE_NONE) == 0 )
      ? copyStr(memGetLine(po->book, lnum, false)) //Copy, the next ml_get() will invalidate it
      : null; //We only care about the return value, not the actual string comparisons.

   int off = lnum - dp->lnum[idx];

   for (Unt i = 0; i < DB_COUNT; ++i) {
      if (curtab->diffbuf[i] && i != idx) {
         // Skip lines that are not in the other change (filler lines).
         if (off >= dp->count[i])
            continue;
         added = false;
         if ((diff_flags & DIFF_INLINE_NONE) != 0)
            break; // early terminate as we only care about the return value

         CS line_new = memGetLine(curtab->diffbuf[i], dp->lnum[i] + off, false);

         //Search for start of difference
         si_org = si_new = 0;
         while (line_org[si_org] != ZERO) {
            if (((diff_flags & DIFF_IWHITE)
                   && SPACE_OR_TAB(line_org[si_org])
                           && SPACE_OR_TAB(line_new[si_new]))
               || ((diff_flags & DIFF_IWHITEALL)
                   && (SPACE_OR_TAB(line_org[si_org])
                         || SPACE_OR_TAB(line_new[si_new]))))
            {
                si_org = (int)(skipwhite(line_org + si_org) - line_org);
                si_new = (int)(skipwhite(line_new + si_new) - line_new);
            } else {
               if (!diff_equal_char(line_org + si_org, line_new + si_new, OUT &l))
                  break;
               si_org += l;
               si_new += l;
            }
         }
          
         //Move back to first byte of character in both lines (may
         //have "nn^" in line_org and "n^ in line_new).
         si_org -= mb_head_off(line_org, line_org + si_org);
         si_new -= mb_head_off(line_new, line_new + si_new);
         if (*startp > si_org)
            *startp = si_org;

         // Search for end of difference, if any.
         if (line_org[si_org] != ZERO || line_new[si_new] != ZERO) {
            ei_org = (int)STRLEN(line_org);
            ei_new = (int)STRLEN(line_new);
            while (ei_org >= *startp && ei_new >= si_new && ei_org >= 0 && ei_new >= 0) {
                if (((diff_flags & DIFF_IWHITE)
                  && SPACE_OR_TAB(line_org[ei_org])
                           && SPACE_OR_TAB(line_new[ei_new]))
                   || ((diff_flags & DIFF_IWHITEALL)
                  && (SPACE_OR_TAB(line_org[ei_org])
                         || SPACE_OR_TAB(line_new[ei_new])))
               ) {
                  while (ei_org >= *startp && SPACE_OR_TAB(line_org[ei_org]))
                     --ei_org;
                  while (ei_new >= si_new && SPACE_OR_TAB(line_new[ei_new]))
                     --ei_new;
               } else {
                  p1 = line_org + ei_org;
                  p2 = line_new + ei_new;
                  p1 -= (*mb_head_off)(line_org, p1);
                  p2 -= (*mb_head_off)(line_new, p2);
                  if (!diff_equal_char(p1, p2, OUT &l))
                      break;
                  ei_org -= l;
                  ei_new -= l;
               }
            }
            if (*endp < ei_org)
               *endp = ei_org;
         }
      }
   } 

   eeglFree(line_org);
   return added;
}

//Mapping used for mapping from temporary mmfile created for inline diff back
//to original book's line/col.
typedef struct {
   Long byte_start;
   Long num_bytes;
   int lineoff;
} LinemapEntry;

//Refine inline character-wise diff blocks to create a more human readable
//highlight. Otherwise a naive diff under existing algorithms tends to create
//a messy output with lots of small gaps.
//It does this by merging adjacent long diff blocks if they are only separated
//by a couple characters.
//These are done by heuristics and can be further tuned.
private void
diff_refine_inline_char_highlight(DiffBlock* dp_orig, ArrayList* linemap, int idx1) {
   //Perform multiple passes so that newly merged blocks will now be long
   //enough which may cause other previously unmerged gaps to be merged as well.
   int pass = 1;
   do {
      int has_unmerged_gaps = false;
      int has_merged_gaps = false;
      DiffBlock *dp = dp_orig;
      while (dp && dp->df_next) {
         // Only use first book to calculate the gap because the gap is
         // unchanged text, which would be the same in all books.
         if (dp->lnum[idx1] + dp->count[idx1] - 1 >= linemap[idx1].len
             || dp->df_next->lnum[idx1] - 1 >= linemap[idx1].len
         ) {
            dp = dp->df_next;
            continue;
         }

         // If the gap occurs over different lines, don't consider it
         LinemapEntry *entry1 = 
            &((LinemapEntry*)linemap[idx1].c)[dp->lnum[idx1] + dp->count[idx1] - 1];
         LinemapEntry *entry2 = 
            &((LinemapEntry*)linemap[idx1].c)[dp->df_next->lnum[idx1] - 1];
         if (entry1->lineoff != entry2->lineoff) {
            dp = dp->df_next;
            continue;
         }

         LineNr gap = dp->df_next->lnum[idx1] - (dp->lnum[idx1] + dp->count[idx1]);
         if (gap <= 3) {
            LineNr max_df_count = 0;
            for (int i = 0; i < DB_COUNT; i++)
               max_df_count = MAX(max_df_count, dp->count[i] + dp->df_next->count[i]);

            if (max_df_count >= gap * 4) {
               // Merge current block with the next one. Don't advance the
               // pointer so we try the same merged block against the next one.
               for (int i = 0; i < DB_COUNT; i++) {
                  dp->count[i] = dp->df_next->lnum[i] + dp->df_next->count[i] - dp->lnum[i];
               }
               DiffBlock *dp_next = dp->df_next;
               dp->df_next = dp_next->df_next;
               clear_diffblock(dp_next);
               has_merged_gaps = true;
               continue;
            } else
               has_unmerged_gaps = true;
         }
         dp = dp->df_next;
      }
      if (!has_unmerged_gaps || !has_merged_gaps)
          break;
   } while (pass++ < 4); // use limited number of passes to avoid excessive looping
}

//Find the inline difference within a diff block among different books.  Do
//this by splitting each block's content into characters or words, and then
//use internal xdiff to calculate the per-character/word diff.  The DiffResult is
//stored in dp instead of returned by the function.
private void
diff_find_change_inline_diff(DiffBlock* dp) {
   ArrayList linemap[DB_COUNT];
   ArrayList file1_str;
   ArrayList file2_str;
   int file1_idx = -1;

   long save_diff_algorithm = diff_algorithm;

   DiffIo dio;
   CLEAR_FIELD(dio);
   ga_init2(&dio.dio_diff.dout_ga, sizeof(char *), 1000);

   // inline diff only supports internal algo
   dio.dio_internal = true;

   // always use indent-heuristics to slide diff splits along whitespace
   diff_algorithm |= INDENT_HEURISTIC;

   // diff_read() has an implicit dependency on curtab->first_diff
   DiffBlock   *orig_diff = curtab->first_diff;
   curtab->first_diff = NULL;

   // diff_read() also uses curtab->diffbuf to determine what's an active book
   Book   *(orig_diffbuf[DB_COUNT]);
   memcpy(orig_diffbuf, curtab->diffbuf, sizeof(orig_diffbuf));

   // Buffers to populate mmfile 1/2 that would be passed to xdiff as memory
   // files. Use a grow array as it is not obvious how much exact space we need.
   ga_init2(&file1_str, 1, 1024);
   ga_init2(&file2_str, 1, 1024);

   // Line map to map from generated mmfiles' line numbers back to original
   // diff blocks' locations. Need this even for char diff because not all
   // characters are 1-byte long / ASCII.
   for (int i = 0; i < DB_COUNT; i++)
      ga_init2(&linemap[i], sizeof(LinemapEntry), 128);

   for (int i = 0; i < DB_COUNT; i++) {
      dio.dio_diff.dout_ga.len = 0;

      Book *book = curtab->diffbuf[i];
      if (book == NULL || book->mem.mfile == NULL)
         continue; // skip book that isn't loaded

      if (dp->count[i] == 0) {
         // skip buffers that don't have any texts in this block so we don't
         // end up marking the entire block as modified in multi-book diff
         curtab->diffbuf[i] = NULL;
         continue;
      }

      if (file1_idx == -1)
          file1_idx = i;

      ArrayList   *curstr = (file1_idx != i) ? &file2_str : &file1_str;

      LineNr numlines = 0;
      curstr->len = 0;

      // Split each line into chars/words and populate fake file book as
      // newline-delimited tokens as that's what xdiff requires.
      for (int off = 0; off < dp->count[i]; off++) {
         CS curline = memGetLine(curtab->diffbuf[i], dp->lnum[i] + off, false);

         int in_keyword = false;

         // iwhiteeol support vars
         int last_white = false;
         int eol_len = -1;
         int eol_linemap_len = -1;
         int eol_numlines = -1;

         CS s;
         for (s = curline; *s != ZERO;) {
            int new_in_keyword = false;
            if (diff_flags & DIFF_INLINE_WORD) {
                //Always use the first book's 'iskeyword' to have a consistent diff. For multibyte
                //chars, only treat alphanumeric chars (class 2) as "word", as other classes such as emojis and
                //CJK ideographs do not usually benefit from word diff as
                //Eegl doesn't have a good way to segment them.
                new_in_keyword = (inpGetClassForBook(s, curtab->diffbuf[file1_idx]) == 2);
            }
            if (in_keyword && !new_in_keyword) {
                ga_append(curstr, NL);
                numlines++;
            }

            if (SPACE_OR_TAB(*s)) {
               if (diff_flags & DIFF_IWHITEALL) {
                  in_keyword = false;
                  s = skipwhite(s);
                  continue;
               } ei ((diff_flags & DIFF_IWHITEEOL) || (diff_flags & DIFF_IWHITE)) {
                  if (!last_white) {
                     eol_len = curstr->len;
                     eol_linemap_len = linemap[i].len;
                     eol_numlines = numlines;
                     last_white = true;
                  }
               }
            } else {
               if ((diff_flags & DIFF_IWHITEEOL) || (diff_flags & DIFF_IWHITE)) {
                  last_white = false;
                  eol_len = -1;
                  eol_linemap_len = -1;
                  eol_numlines = -1;
               }
           }

           int char_len = 1;
           if (*s == NL)
               // NL is internal substitute for ZERO
               ga_append(curstr, ZERO);
           else {
               char_len = utfCharLen(s);

               if (SPACE_OR_TAB(*s) && (diff_flags & DIFF_IWHITE))
                  // Treat the entire white space span as a single char.
                  char_len = skipwhite(s) - s;

               if (diff_flags & DIFF_ICASE) {
                  int c;
                  Byte cbuf[MB_MAXBYTES + 1];
                  // xdiff doesn't support ignoring case, fold-case the text manually.
                  c = mb_ptr2char(s);
                  int c_len = MB_CHAR2LEN(c);
                  c = MB_CASEFOLD(c);
                  int c_fold_len = mb_char2bytes(c, cbuf);
                  ga_concat_len(curstr, cbuf, c_fold_len);
                  if (char_len > c_len) {
                      // There may be remaining composing characters. Write those back in.
                      // Composing characters don't need case folding.
                      ga_concat_len(curstr, s + c_len, char_len - c_len);
                  }
               } else
                  ga_concat_len(curstr, s, char_len);
            }

            if (!new_in_keyword) {
               ga_append(curstr, NL);
               numlines++;
            }

            if (!new_in_keyword || (new_in_keyword && !in_keyword)) {
               //create a new mapping entry from the xdiff mmfile back to original line/col.
               LinemapEntry linemap_entry;
               linemap_entry.lineoff = off;
               linemap_entry.byte_start = s - curline;
               linemap_entry.num_bytes = char_len;
               if (ga_grow(&linemap[i], 1) != OK)
                  goto done;
               ((LinemapEntry*)(linemap[i].c))[linemap[i].len] = linemap_entry;
               linemap[i].len += 1;
            } else {
               // Still inside a keyword. Just increment byte count but don't make a new entry.
               // linemap always has at least one entry here
               ((LinemapEntry*)linemap[i].c)[linemap[i].len-1].num_bytes += char_len;
            }

            in_keyword = new_in_keyword;
            s += char_len;
         }
         if (in_keyword) {
            ga_append(curstr, NL);
            numlines++;
         }

         if ((diff_flags & DIFF_IWHITEEOL) || (diff_flags & DIFF_IWHITE)) {
            // Need to trim trailing whitespace. Do this simply by
            // resetting arrays back to before we encountered them.
            if (eol_len != -1) {
               curstr->len = eol_len;
               linemap[i].len = eol_linemap_len;
               numlines = eol_numlines;
            }
         }

         if (!(diff_flags & DIFF_IWHITEALL)) {
            // Add an empty line token mapped to the end-of-line in the
            // original file. This helps diff newline differences among
            // files, which will be visualized when using 'list' as the eol
            // listchar will be highlighted.
            ga_append(curstr, NL);
            numlines++;

            LinemapEntry linemap_entry;
            linemap_entry.lineoff = off;
            linemap_entry.byte_start = s - curline;
            linemap_entry.num_bytes = sizeof(NL);
            if (ga_grow(&linemap[i], 1) != OK)
                goto done;
            ((LinemapEntry*)(linemap[i].c))[linemap[i].len] = linemap_entry;
            linemap[i].len += 1;
         }
      }

      if (file1_idx != i) {
         dio.new.mmfile.ptr = curstr->c;
         dio.new.mmfile.size = curstr->len;
      } else {
         dio.orig.mmfile.ptr = curstr->c;
         dio.orig.mmfile.size = curstr->len;
      }
      if (file1_idx != i) {
         // Perform diff with first file and read the results
         int diff_status = diff_file_internal(&dio);
         if (diff_status == FAIL)
            goto done;

         diff_read(0, i, &dio);
         clear_diffout(&dio.dio_diff);
      }
   }
   DiffBlock *new_diff = curtab->first_diff;

   if (diff_flags & DIFF_INLINE_CHAR && file1_idx != -1)
      diff_refine_inline_char_highlight(new_diff, linemap, file1_idx);

   //After the diff, use the linemap to obtain the original line/col of the
   //changes and cache them in dp.
   dp->changes.len = 0; // this should already be zero
   for (; new_diff; new_diff = new_diff->df_next) {
      DifflineChange change;
      CLEAR_FIELD(change);
      for (int i = 0; i < DB_COUNT; i++) {
         if (new_diff->lnum[i] <= 0) // should never be < 0. Checking just for safety.
            continue;
         LineNr diff_lnum = new_diff->lnum[i] - 1; // use zero-index
         LineNr diff_lnum_end = diff_lnum + new_diff->count[i];

         if (diff_lnum >= linemap[i].len) {
            change.dc_start[i] = MAXCOL;
            change.dc_start_lnum_off[i] = INT_MAX;
         } else {
            change.dc_start[i] = ((LinemapEntry*)linemap[i].c)[diff_lnum].byte_start;
            change.dc_start_lnum_off[i] = ((LinemapEntry*)linemap[i].c)[diff_lnum].lineoff; }

         if (diff_lnum == diff_lnum_end) {
            change.dc_end[i] = change.dc_start[i];
            change.dc_end_lnum_off[i] = change.dc_start_lnum_off[i];
         } ei (diff_lnum_end - 1 >= linemap[i].len) {
            change.dc_end[i] = MAXCOL;
            change.dc_end_lnum_off[i] = INT_MAX;
         } else {
            change.dc_end[i] = ((LinemapEntry*)linemap[i].c)[diff_lnum_end-1].byte_start +
                ((LinemapEntry*)linemap[i].c)[diff_lnum_end-1].num_bytes;
            change.dc_end_lnum_off[i] = ((LinemapEntry*)linemap[i].c)[diff_lnum_end-1].lineoff;
         }
      }
      if (ga_grow(&dp->changes, 1) != OK) {
          dp->changes.len = 0;
          goto done;
      }
      ((DifflineChange*)(dp->changes.c))[dp->changes.len] = change;
      dp->changes.len += 1;
   }

done:
   diff_algorithm = save_diff_algorithm;

   dp->has_changes = true;

   diff_clear(curtab);
   curtab->first_diff = orig_diff;
   memcpy(curtab->diffbuf, orig_diffbuf, sizeof(orig_diffbuf));

   ga_clear(&file1_str);
   ga_clear(&file2_str);
   // No need to clear dio.orig/new because they were referencing
   // strings that are now cleared.
   clear_diffout(&dio.dio_diff);
   for (int i = 0; i < DB_COUNT; i++)
      ga_clear(&linemap[i]);
}

// Find the difference within a changed line.
// Return true if the line was added, no other book has it.
int
diff_find_change(Portal* po, LineNr lnum, DiffLine* diffline){
   Unt idx = bookIndex(po->book);
   if (idx == UNT)   // cannot happen
      return false;

   // search for a change that includes "lnum" in the list of diffblocks.
   DiffBlock* dp;
   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (lnum < dp->lnum[idx] + dp->count[idx])
         break;
   } 
   if (!dp || checkSanity(curtab, dp) == FAIL)
      return false;

   if (lnum - dp->lnum[idx] > INT_MAX)
      // Integer overflow protection
      return false;
   int off = lnum - dp->lnum[idx];

   if (!(diff_flags & ALL_INLINE_DIFF) || diff_internal_failed()) {
      // Use simple algorithm
      int   change_start = MAXCOL;   // first col of changed area
      int   change_end = -1;   // last col of changed area
      int   ret;

      ret = diff_find_change_simple(po, lnum, dp, idx, OUT &change_start, OUT &change_end);

      // convert from inclusive end to exclusive end per diffline's contract
      change_end += 1;

      // Create a mock diffline struct. We always only have one so no need to allocate memory.
      CLEAR_FIELD(simple_diffline_change);
      diffline->changes = &simple_diffline_change;
      diffline->num_changes = 1;
      diffline->bufidx = idx;
      diffline->lineoff = lnum - dp->lnum[idx];

      simple_diffline_change.dc_start[idx] = change_start;
      simple_diffline_change.dc_end[idx] = change_end;
      simple_diffline_change.dc_start_lnum_off[idx] = off;
      simple_diffline_change.dc_end_lnum_off[idx] = off;
      return ret;
   }

   // Use inline diff algorithm. The diff changes are usually cached so we check that first.
   if (!dp->has_changes)
      diff_find_change_inline_diff(dp);

   ArrayList *changes = &dp->changes;

   //Use linear search to find the first change for this line. We could
   //optimize this to use binary search, but there should usually be a
   //limited number of inline changes per diff block, and limited number of
   //diff blocks shown on screen, so it is not necessary.
   int num_changes = 0;
   int change_idx = 0;
   diffline->changes = NULL;
   for (change_idx = 0; change_idx < changes->len; change_idx++) {
      DifflineChange *change = &((DifflineChange*)dp->changes.c)[change_idx];
      if (change->dc_end_lnum_off[idx] < off)
          continue;
      if (change->dc_start_lnum_off[idx] > off)
          break;
      if (diffline->changes == NULL)
          diffline->changes = change;
      num_changes++;
   }
   diffline->num_changes = num_changes;
   diffline->bufidx = idx;
   diffline->lineoff = off;

   // Detect simple cases of added lines in the end within a diff block. This
   // has to be the last change of this diff block, and all other buffers are
   // considering this to be an addition past their last line. Other scenarios
   // will be considered a changed line instead.
   int added = false;
   if (num_changes == 1 && change_idx == dp->changes.len) {
      added = true;
      for (Unt i = 0; i < DB_COUNT; i++) {
         if (idx == i)
            continue;
         if (curtab->diffbuf[i] == NULL)
            continue;
         DifflineChange *change = &((DifflineChange*)dp->changes.c)[dp->changes.len-1];
         if (change->dc_start_lnum_off[i] != INT_MAX) {
            added = false;
            break;
         }
      }
   }
   return added;
}

//Return true if line "lnum" is not close to a diff block, this line should be in a fold.
//Return false if there are no diff blocks at all in this portal.
int
diff_infold(Portal *po, LineNr lnum) {
   int      i;
   Unt idx = UNT;
   int      other = false;
   DiffBlock   *dp;

   // Return if 'diff' isn't set.
   if (!po->o.diff)
      return false;

   for (i = 0; i < DB_COUNT; ++i) {
      if (curtab->diffbuf[i] == po->book)
         idx = i;
      ei (curtab->diffbuf[i] != NULL)
         other = true;
   }

   // return here if there are no diffs in the portal
   if (idx == UNT || !other)
      return false;

   if (curtab->diff_invalid)
      c_diffupdate(NULL);      // update after a big change

   // Return if there are no diff blocks.  All lines will be folded.
   if (curtab->first_diff == NULL)
      return true;

   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      // If this change is below the line there can't be any further match.
      if (dp->lnum[idx] - diff_context > lnum)
          break;
      // If this change ends before the line we have a match.
      if (dp->lnum[idx] + dp->count[idx] + diff_context > lnum)
          return false;
   }
   return true;
}

//"dp" and "do" commands.
void
nvDiffGetPut(int put, long count) {
   Invocation   ea;
   Byte   buf[30];

   if (bt_prompt(curBook)) {
      return;
   }
   if (count == 0)
      ea.arg = S"";
   else {
      eeSnprintf(buf, 30, "%ld", count);
      ea.arg = buf;
   }
   if (put)
      ea.id = C_diffput;
   else
      ea.id = C_diffget;
    ea.addr_count = 0;
    ea.line1 = curPor->cursor.lnum;
    ea.line2 = curPor->cursor.lnum;
    c_diffgetput(&ea);
}

//Return true if "diff" appears in the list of diff blocks of the current tab.
private int
valid_diff(DiffBlock *diff) {
   DiffBlock   *dp;
   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (dp == diff)
          return true;
   } 
   return false;
}

// ":diffget", ":diffput"
void
c_diffgetput(Invocation* invo) {
   LineNr lnum;
   int count;
   LineNr off = 0;
   DiffBlock* dp;
   DiffBlock* dprev;
   DiffBlock* dfree;
   Unt      idx_other;
   int      i;
   int      added;
   Byte   *p;
   AutocommSave   aco;
   Book* book;
   int start_skip, end_skip;
   int new_count;
   int buf_empty;
   int found_not_ma = false;

   // Find the current book in the list of diff buffers.
   Unt idx_cur = bookIndex(curBook);
   if (idx_cur == UNT) {
      emsg(_(e_current_buffer_is_not_in_diff_mode));
      return;
   }

   if (*invo->arg == ZERO) {
      // No argument: Find the other book in the list of diff buffers.
      for (idx_other = 0; idx_other < DB_COUNT; ++idx_other) {
         if (curtab->diffbuf[idx_other] != curBook && curtab->diffbuf[idx_other] != NULL) {
            if (invo->id != C_diffput || curtab->diffbuf[idx_other]->o.modifiable)
               break;
            found_not_ma = true;
         }
      } 
      if (idx_other == DB_COUNT) {
         if (found_not_ma)
            emsg(_(e_no_other_buffer_in_diff_mode_is_modifiable));
         else
            emsg(_(e_no_other_buffer_in_diff_mode));
         return;
      }

      // Check that there isn't a third book in the list
      for (i = idx_other + 1; i < DB_COUNT; ++i)
         if (curtab->diffbuf[i] != curBook
             && curtab->diffbuf[i] != NULL
             && (invo->id != C_diffput || curtab->diffbuf[i]->o.modifiable)
         ) {
            emsg(_(e_more_than_two_buffers_in_diff_mode_dont_know_which_one_to_use));
            return;
         }
   } else {
      // Book number or pattern given.  Ignore trailing white space.
      p = invo->arg + STRLEN(invo->arg);
      while (p > invo->arg && SPACE_OR_TAB(p[-1]))
         --p;
      for (i = 0; eeIsDigit(invo->arg[i]) && invo->arg + i < p; ++i)
         ;
      if (invo->arg + i == p)       // digits only
         i = atol((char *)invo->arg);
      else {
         i = booklistFindPattern(invo->arg, p, false, true, false);
         if (i < 0)
            return;      // error message already given
      }
      book = bookFindFileByBookNr(i);
      if (!book) {
         showErrFmtMsg(_(e_cant_find_book_str), invo->arg);
         return;
      }
      if (book == curBook)
         return;      // nothing to do
      idx_other = bookIndex(book);
      if (idx_other == DB_COUNT) {
         showErrFmtMsg(_(e_buffer_str_is_not_in_diff_mode), invo->arg);
         return;
      }
   }

   isBusyP = true;

   //When no range given include the line above or below the cursor.
   if (invo->addr_count == 0) {
      //Make it possible that ":diffget" on the last line gets line below
      //the cursor line when there is no difference above the cursor.
      LineDiffStatus linestatus = LINE_STATUS_UNCHANGED;
      if (invo->line1 == curBook->mem.lineCount
         && (diff_check_with_linestatus(curPor, invo->line1, OUT &linestatus) == 0
             && linestatus == LINE_STATUS_UNCHANGED)
         && (invo->line1 == 1 ||
             (diff_check_with_linestatus(curPor, invo->line1 - 1, OUT &linestatus) >= 0
              && linestatus == LINE_STATUS_UNCHANGED))
      )
         ++invo->line2;
      ei (invo->line1 > 0)
         --invo->line1;
   }
   Unt idx_to, idx_from;
   if (invo->id == C_diffget) {
      idx_from = idx_other;
      idx_to = idx_cur;
   } else {
      idx_from = idx_cur;
      idx_to = idx_other;
      // Need to make the other book the current book to be able to make changes in it.
      // Set curPor/curBook to book and save a few things.
      auCommPrepareBook(&aco, curtab->diffbuf[idx_other]);
      if (curBook != curtab->diffbuf[idx_other])
          // Could not find a portal for this book, the rest is likely to fail.
          goto theend;
    }

   // May give the warning for a changed book here, which can trigger the FileChangedRO 
   // autocommand, which may do nasty things and mess everything up.
   if (!curBook->wasModified) {
      change_warning(0);
      if (bookIndex(curBook) != idx_to) {
         emsg(_(e_buffer_changed_unexpectedly));
         goto theend;
      }
   }

   dprev = NULL;
   for (dp = curtab->first_diff; dp != NULL; ) {
      if (!invo->addr_count) {
         //Handle the case with adjacent diff blocks (e.g. using linematch or anchors) at/above 
         //the cursor. Since a range wasn't specified, we just want to grab one diff block rather 
         //than all of them in the vicinity.
         while (dp->df_next
             && dp->df_next->lnum[idx_cur] == dp->lnum[idx_cur] + dp->count[idx_cur]
             && dp->df_next->lnum[idx_cur] == invo->line1 + off + 1
         ) {
            dprev = dp;
            dp = dp->df_next;
         }
      }
      if (dp->lnum[idx_cur] > invo->line2 + off)
         break;   // past the range that was specified

      dfree = NULL;
      lnum = dp->lnum[idx_to];
      count = dp->count[idx_to];
      if (dp->lnum[idx_cur] + dp->count[idx_cur] > invo->line1 + off
         && u_save(lnum - 1, lnum + count) != FAIL
      ) {
         // Inside the specified range and saving for undo worked.
         start_skip = 0;
         end_skip = 0;
         if (invo->addr_count > 0) {
            // A range was specified: check if lines need to be skipped.
            start_skip = invo->line1 + off - dp->lnum[idx_cur];
            if (start_skip > 0) {
               // range starts below start of current diff block
               if (start_skip > count) {
                  lnum += count;
                  count = 0;
               } else {
                  count -= start_skip;
                  lnum += start_skip;
               }
            } else
               start_skip = 0;

            end_skip = dp->lnum[idx_cur] + dp->count[idx_cur] - 1 - (invo->line2 + off);
            if (end_skip > 0) {
               // range ends above end of current/from diff block
               if (idx_cur == idx_from) {  // :diffput
               i = dp->count[idx_cur] - start_skip - end_skip;
               if (count > i)
                   count = i;
               } else {        // :diffget
                  count -= end_skip;
                  end_skip = dp->count[idx_from] - start_skip - count;
                  if (end_skip < 0)
                      end_skip = 0;
               }
            } else
               end_skip = 0;
         }

         buf_empty = CURBOOK_EMPTY();
         added = 0;
         for (i = 0; i < count; ++i) {
            // remember deleting the last line of the book
            buf_empty = curBook->mem.lineCount == 1;
            if (ml_delete(lnum) == OK)
               --added;
         }
         for (i = 0; i < dp->count[idx_from] - start_skip - end_skip; ++i) {
            LineNr nr = dp->lnum[idx_from] + start_skip + i;
            if (nr > curtab->diffbuf[idx_from]->mem.lineCount)
                break;
            p = copyStr(memGetLine(curtab->diffbuf[idx_from], nr, false));
            ml_append(lnum + i - 1, p, 0, false);
            eeglFree(p);
            ++added;
            if (buf_empty && curBook->mem.lineCount == 2) {
               //Added the first line into an empty book, need to delete the dummy empty line.
               buf_empty = false;
               ml_delete((LineNr)2);
            }
         }
         new_count = dp->count[idx_to] + added;
         dp->count[idx_to] = new_count;

         if (start_skip == 0 && end_skip == 0) {
            // Check if there are any other buffers and if the diff is equal in them.
            Unt i;
            for (i = 0; i < DB_COUNT; ++i) {
               if (curtab->diffbuf[i] 
                      && i != idx_from && i != idx_to
                      && !diff_equal_entry(dp, idx_from, i)
               )
                  break;
            } 
            if (i == DB_COUNT) {
               // delete the diff entry, the buffers are now equal here
               dfree = dp;
               dp = dp->df_next;
               if (dprev == NULL)
                  curtab->first_diff = dp;
               else
                  dprev->df_next = dp;
            }
         }

         if (added != 0) {
            //Adjust marks. This will change the following entries!
            markAdjust(lnum, lnum + count - 1, (long)MAXLNUM, (long)added, true);
            if (curPor->cursor.lnum >= lnum) {
               // Adjust the cursor position if it's in/after the changed lines.
               if (curPor->cursor.lnum >= lnum + count)
                  curPor->cursor.lnum += added;
               ei (added < 0)
                  curPor->cursor.lnum = lnum;
            }
         }
         changed_lines(lnum, 0, lnum + count, (long)added);

         if (dfree) {
            // Diff is deleted, update folds in other portals.
            diff_fold_update(dfree, idx_to);
            clear_diffblock(dfree);
         }

         //markAdjust() may have made "dp" invalid.  We don't know where to continue then, bail out
         if (added != 0 && !valid_diff(dp))
            break;

         if (!dfree)
            // markAdjust() may have changed the count in a wrong way
            dp->count[idx_to] = new_count;

         // When changing the current book, keep track of line numbers
         if (idx_cur == idx_to)
            off += added;
      }

      // If before the range or not deleted, go to next diff.
      if (dfree == NULL) {
         dprev = dp;
         dp = dp->df_next;
      }
   }

   // restore curPor/curBook and a few other things
   if (invo->id != C_diffget) {
      //Syncing undo only works for the current book, but we change another book. Sync undo if the 
      //command was typed. This isn't 100% right when ":diffput" is used in a function or mapping
      if (keyWasTypedG)
         u_sync(false);
      auCommRestoreBook(&aco);
   }

theend:
   isBusyP = false;
   if (needUpdateP)
      c_diffupdate(NULL);

   // Check that the cursor is on a valid character and update its
   // position.  When there were filler lines the topline has become invalid.
   check_cursor();
   changed_line_abv_curs();

   // If all diffs are gone, update folds in all diff portals.
   if (curtab->first_diff == NULL) {
      Portal* po;
      FOR_ALL_PORTALS_IN_TAB(curtab, po) {
         if (po->o.diff && po->o.foldMethod == FOLD_DIFF && po->o.foldEnable)
            foldUpdateAll(po);
      } 
   }

   if (needUpdateP)
      // redraw already done by c_diffupdate()
      needUpdateP = false;
   else {
      // Also need to redraw the other books.
      diff_redraw(false);
      applyAutocomms(EVENT_DIFFUPDATED, NULL, NULL, false, curBook);
   }
}

//Update folds for all diff buffers for entry "dp".
//Skip book with index "skip_idx". When there are no diffs, all folds are removed.
private void
diff_fold_update(DiffBlock* dp, Unt skip_idx) {
   Portal   *po;
   FOR_ALL_PORTALS(po) {
      for (Unt i = 0; i < DB_COUNT; ++i)
         if (curtab->diffbuf[i] == po->book && i != skip_idx)
            foldUpdate(po, dp->lnum[i], dp->lnum[i] + dp->count[i]);
   } 
}

//Return true if book "book" is in diff-mode.
Boole
diffIsBookInDiffMode(Book* book) {
   Tab* t;
   FOR_ALL_TABS(t) {
      if (bookIndexInTab(book, t) != UNT)
         return true;
   } 
   return false;
}

//Move "count" times in direction "dir" to the next diff block.
//Return FAIL if there isn't such a diff block.
int
diff_move_to(int dir, long count) {
   LineNr   lnum = curPor->cursor.lnum;
   DiffBlock   *dp;

   Unt idx = bookIndex(curBook);
   if (idx == UNT || curtab->first_diff == NULL)
      return FAIL;

   if (curtab->diff_invalid)
      c_diffupdate(NULL);      // update after a big change

   if (curtab->first_diff == NULL)      // no diffs today
      return FAIL;

   while (--count >= 0) {
      // Check if already before first diff.
      if (dir == BACKWARD && lnum <= curtab->first_diff->lnum[idx])
          break;

      for (dp = curtab->first_diff; ; dp = dp->df_next) {
         if (!dp)
            break;
         if ((dir == FORWARD && lnum < dp->lnum[idx])
             || (dir == BACKWARD && (dp->df_next == NULL || lnum <= dp->df_next->lnum[idx]))
         ) {
            lnum = dp->lnum[idx];
            break;
         }
      }
   }

   // don't end up past the end of the file
   if (lnum > curBook->mem.lineCount)
      lnum = curBook->mem.lineCount;

   // When the cursor didn't move at all we fail.
   if (lnum == curPor->cursor.lnum)
      return FAIL;

   setpcmark();
   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;

   return OK;
}

//Return the line number in the current portal that is closest to "lnum1" in "book" in diff mode.
private LineNr
diff_get_corresponding_line_int(Book* book, LineNr lnum1) {
   DiffBlock* dp;
   int baseline = 0;

   Unt idx1 = bookIndex(book);
   Unt idx2 = bookIndex(curBook);
   if (idx1 == UNT || idx2 == UNT || curtab->first_diff == NULL)
      return lnum1;

   if (curtab->diff_invalid)
      c_diffupdate(NULL);      // update after a big change

   if (curtab->first_diff == NULL)      // no diffs today
      return lnum1;

   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (dp->lnum[idx1] > lnum1)
          return lnum1 - baseline;
      if ((dp->lnum[idx1] + dp->count[idx1]) > lnum1) {
          // Inside the diffblock
          baseline = lnum1 - dp->lnum[idx1];
          if (baseline > dp->count[idx2])
         baseline = dp->count[idx2];

          return dp->lnum[idx2] + baseline;
      }
      if (    (dp->lnum[idx1] == lnum1)
           && (dp->count[idx1] == 0)
           && (dp->lnum[idx2] <= curPor->cursor.lnum)
           && ((dp->lnum[idx2] + dp->count[idx2]) > curPor->cursor.lnum)
      )
          //Special case: if the cursor is just after a zero-count
          //block (i.e. all filler) and the target cursor is already
          //inside the corresponding block, leave the target cursor
          //unmoved. This makes repeated CTRL-W W operations work as expected.
          return curPor->cursor.lnum;
      baseline = (dp->lnum[idx1] + dp->count[idx1]) - (dp->lnum[idx2] + dp->count[idx2]);
   }

    // If we get here then the cursor is after the last diff
    return lnum1 - baseline;
}

//Return the line number in the current portal that is closest to "lnum1" in
//"buf1" in diff mode.  Checks the line number to be valid.
LineNr
diff_get_corresponding_line(Book* book1, LineNr lnum1) {
   LineNr lnum = diff_get_corresponding_line_int(book1, lnum1);

   // don't end up past the end of the file
   if (lnum > curBook->mem.lineCount)
      return curBook->mem.lineCount;
   return lnum;
}

//For line "lnum" in the current portal find the equivalent lnum in portal
//"po", compensating for inserted/deleted lines.
LineNr
diff_lnum_win(LineNr lnum, Portal *po) {
   DiffBlock   *dp;

   Unt idx = bookIndex(curBook);
   if (idx == UNT)      // safety check
      return (LineNr)0;

   if (curtab->diff_invalid)
      c_diffupdate(NULL);      // update after a big change

   // search for a change that includes "lnum" in the list of diffblocks.
   FOR_ALL_DIFFBLOCKS_IN_TAB(curtab, dp) {
      if (lnum <= dp->lnum[idx] + dp->count[idx])
          break;
   }

   // When after the last change, compute relative to the last line number.
   if (dp == NULL)
      return po->book->mem.lineCount - (curBook->mem.lineCount - lnum);

   // Find index for "po".
   Unt i = bookIndex(po->book);
   if (i == UNT)         // safety check
      return (LineNr)0;

   LineNr n = lnum + (dp->lnum[i] - dp->lnum[idx]);
   if (n > dp->lnum[i] + dp->count[i])
      n = dp->lnum[i] + dp->count[i];
   return n;
}

//Handle an ED style diff line. Return FAIL if the line does not contain diff info.
private int
parse_diff_ed(CS line, Hunk* hunk) {
   // The line must be one of three formats:
   // change: {first}[,{last}]c{first}[,{last}]
   // append: {first}a{first}[,{last}]
   // delete: {first}[,{last}]d{first}
   CS p = line;
   long f1 = parseLong(&p);
   long l1;
   if (*p == ',') {
      ++p;
      l1 = parseLong(&p);
   } else
      l1 = f1;
   if (*p != 'a' && *p != 'c' && *p != 'd')
      return FAIL;      // invalid diff format
   int difftype = *p++;
   long f2 = parseLong(&p);
   long l2; 
   if (*p == ',') {
      ++p;
      l2 = parseLong(&p);
   } else
      l2 = f2;
   if (l1 < f1 || l2 < f2)
      return FAIL;

   if (difftype == 'a') {
      hunk->origLnum = f1 + 1;
      hunk->origCount = 0;
   } else {
      hunk->origLnum = f1;
      hunk->origCount = l1 - f1 + 1;
   }
   if (difftype == 'd') {
      hunk->newLnum = f2 + 1;
      hunk->newCount = 0;
   } else {
      hunk->newLnum = f2;
      hunk->newCount = l2 - f2 + 1;
   }
   return OK;
}

//Parse unified diff with zero(!) context lines.
//Return FAIL if there is no diff information in "line".
private int
parse_diff_unified(CS line, Hunk* hunk) {
   long    oldline, oldcount, newline, newcount;

   //Parse unified diff hunk header: @@ -oldline,oldcount +newline,newcount @@
   CS p = line;
   if (*p++ == '@' && *p++ == '@' && *p++ == ' ' && *p++ == '-') {
      oldline = parseLong(&p);
      if (*p == ',') {
         ++p;
         oldcount = parseLong(&p);
      } else
         oldcount = 1;
      if (*p++ == ' ' && *p++ == '+') {
         newline = parseLong(&p);
         if (*p == ',') {
            ++p;
            newcount = parseLong(&p);
         } else
            newcount = 1;
      } else
          return FAIL;   // invalid diff format

      if (oldcount == 0)
         oldline += 1;
      if (newcount == 0)
         newline += 1;
      if (newline == 0)
         newline = 1;

      hunk->origLnum = oldline;
      hunk->origCount = oldcount;
      hunk->newLnum = newline;
      hunk->newCount = newcount;

      return OK;
   }

   return FAIL;
}

//Callback function for the xdl_diff() function. Store the diff output (indices) in an arraylist
private int
xdiff_out_indices(
   long start_a,
   long count_a,
   long start_b,
   long count_b,
   void* priv
) {
   DiffResult* dout = (DiffResult *)priv;
   Hunk* p = ALLOC_ONE(Hunk);

   if (ga_grow(&dout->dout_ga, 1) == FAIL) {
      eeglFree(p);
      return -1;
   }

   p->origLnum  = start_a + 1;
   p->origCount = count_a;
   p->newLnum   = start_b + 1;
   p->newCount  = count_b;
   ((Hunk **)dout->dout_ga.c)[dout->dout_ga.len++] = p;
   return 0;
}

//Callback function for the xdl_diff() function. Store the unified diff output in a grow array.
private int
xdiff_out_unified(
   void* priv,
   MmBuffer* mb,
   int nbuf
){
   DiffResult* dout = (DiffResult *)priv;
   for (int i = 0; i < nbuf; i++)
      ga_concat_len(&dout->dout_ga, (CS)mb[i].ptr, mb[i].size);

   return 0;
}

void
f_diff_filler(Var *argvars UNUSED, Var *returnVar UNUSED) {
   returnVar->number = diff_check_fill(curPor, tv_get_lnum(argvars));
}

void
f_diff_hlID(Var* argvars, Var* returnVar) {
   static LineNr prev_lnum = 0;
   static Long   changedtick = 0;
   static int fnum = 0;
   static Unt prev_diff_flags = 0;
   static int change_start = 0;
   static int change_end = 0;
   static Short hlID = 0;
   int         cache_results = true;
   int         col;
   DiffLine diffline;

   CLEAR_FIELD(diffline);

   if (diff_flags & ALL_INLINE_DIFF) {
      // Remember the results if using simple since it's recalculated per
      // call. Otherwise just call diff_find_change() every time since
      // internally the DiffResult is cached internally.
      cache_results = false;
   }

   LineNr lnum = tv_get_lnum(argvars);
   if (lnum < 0)   // ignore type error in {lnum} arg
      lnum = 0;
   if (!cache_results
       || lnum != prev_lnum
       || changedtick != CHANGEDTICK(curBook)
       || fnum != curBook->fiNum
       || diff_flags != prev_diff_flags
   ){
      // New line, buffer, change: need to get the values.
      LineDiffStatus linestatus = LINE_STATUS_UNCHANGED;
      diff_check_with_linestatus(curPor, lnum, OUT &linestatus);
      if (linestatus != LINE_STATUS_UNCHANGED) {
         if (linestatus == LINE_STATUS_CHANGED) {
            change_start = MAXCOL;
            change_end = -1;
            if (diff_find_change(curPor, lnum, &diffline))
               hlID = HLF_ADD;   // added line
            else {
               hlID = HLF_CHD;   // changed line
               if (diffline.num_changes > 0 && cache_results) {
                  change_start = diffline.changes[0].dc_start[diffline.bufidx];
                  change_end = diffline.changes[0].dc_end[diffline.bufidx];
               }
            }
         } else
            hlID = HLF_ADD;   // added line
      } else
         hlID = 0; // NORMAL hilite group

      if (cache_results) {
         prev_lnum = lnum;
         changedtick = CHANGEDTICK(curBook);
         fnum = curBook->fiNum;
         prev_diff_flags = diff_flags;
      }
   }

   if (hlID == HLF_CHD || hlID == HLF_TXD) {
      col = tv_get_number(&argvars[1]) - 1; // ignore type error in {col}
      if (cache_results) {
         if (col >= change_start && col < change_end)
            hlID = HLF_TXD;         // changed text
         else
            hlID = HLF_CHD;         // changed line
      } else {
         hlID = HLF_CHD;
         for (int i = 0; i < diffline.num_changes; i++) {
            int added = diff_change_parse(
                  &diffline, &diffline.changes[i], &change_start, &change_end
            );
            if (col >= change_start && col < change_end) {
               hlID = added ? HLF_TXA : HLF_TXD;
               break;
            }
            if (col < change_start)
               // the remaining changes are past this column and not relevant
               break;
         }
      }
   }
   returnVar->number = hlID == 0 ? 0 : (int)hlID;
}

//Parse the diff options passed in "optarg" to the diff() function and return
//the options in "diffopts" and the diff algorithm in "diffalgo".
private int
parse_diff_optarg(
   Var* opts,
   Unt* diffopts,
   long* diffalgo,
   OutputFormat* diff_output_fmt,
   OUT int* diff_ctxlen
) {
   Bag* d = opts->bag;

   CS algo = bagGetString(d, tConst("algorithm"), false);
   if (algo) {
      if (STRNCMP(algo, "myers", 5) == 0)
         *diffalgo = 0;
      ei (STRNCMP(algo, "minimal", 7) == 0)
         *diffalgo = NEED_MINIMAL;
      ei (STRNCMP(algo, "patience", 8) == 0)
         *diffalgo = PATIENCE_DIFF;
      ei (STRNCMP(algo, "histogram", 9) == 0)
         *diffalgo = XDF_HISTOGRAM_DIFF;
   }

   CS output_fmt = bagGetString(d, tConst("output"), false);
   if (output_fmt) {
      if (STRNCMP(output_fmt, "unified", 7) == 0)
          *diff_output_fmt = DIO_OUTPUT_UNIFIED;
      ei (STRNCMP(output_fmt, "indices", 7) == 0)
          *diff_output_fmt = DIO_OUTPUT_INDICES;
      else {
         showErrFmtMsg(_(e_unsupported_diff_output_format_str), output_fmt);
         return FAIL;
      }
   }

   *diff_ctxlen = bagGetNumber_def(d, tConst("context"), -1);
   if (*diff_ctxlen < 0)
      *diff_ctxlen = 0;

   if (bagGetBool(d, tConst("iblank"), false))
      *diffopts |= DIFF_IBLANK;
   if (bagGetBool(d, tConst("icase"), false))
      *diffopts |= DIFF_ICASE;
   if (bagGetBool(d, tConst(S"iwhite"), false))
      *diffopts |= DIFF_IWHITE;
   if (bagGetBool(d, tConst("iwhiteall"), false))
      *diffopts |= DIFF_IWHITEALL;
   if (bagGetBool(d, tConst("iwhiteeol"), false))
      *diffopts |= DIFF_IWHITEEOL;
   if (bagGetBool(d, tConst("indent-heuristic"), false))
      *diffalgo |= INDENT_HEURISTIC;

   return OK;
}

//Concatenate the List of strings in "l" and store the DiffResult in
//"din->mmfile.ptr" and the length in "din->mmfile.size".
private void
list_to_diffin(List* l, DiffInp* din, int icase) {
   ArrayList   ga;
   ListItem* li;

   ga_init2(&ga, 1, 2048);

   FOR_ALL_LIST_ITEMS(l, li) {
      CS str = tv_get_string(&li->c);
      if (icase) {
         str = strlow_save(str);
         if (!str)
            continue;
      }
      ga_concat(&ga, str);
      ga_append(&ga, NL);
      if (icase)
         eeglFree(str);
   }

   din->mmfile.ptr = (Byte *)ga.c;
   din->mmfile.size = ga.len;
}

//Get the start and end indices from the diff "hunk".
private Bag *
get_diff_hunk_indices(Hunk* hunk) {
   Bag* hunkBag = allocBag();
   bagAddNumber(hunkBag, S"from_idx", hunk->origLnum - 1);
   bagAddNumber(hunkBag, S"from_count", hunk->origCount);
   bagAddNumber(hunkBag, S"to_idx", hunk->newLnum  - 1);
   bagAddNumber(hunkBag, S"to_count", hunk->newCount);

   return hunkBag;
}

void
f_diff(Var* argvars, Var* returnVar) {
   if (confirmVarIsNonnullList(argvars, 0) == FAIL
        || confirmVarIsNonnullList(argvars, 1) == FAIL
        || check_for_opt_nonnull_dict_arg(argvars, 2) == FAIL)
      return;

   DiffIo dio;
   CLEAR_FIELD(dio);
   dio.dio_internal = true;
   ga_init2(&dio.dio_diff.dout_ga, sizeof(char *), 1000);

   List* orig_list = argvars[0].list;
   List* new_list = argvars[1].list;

   // Save the 'diffopt' option value and restore it after getting the diff.
   int      save_diff_flags = diff_flags;
   long   save_diff_algorithm = diff_algorithm;
   diff_flags = DIFF_INTERNAL;
   diff_algorithm = 0;
   dio.dio_outfmt = DIO_OUTPUT_UNIFIED;
   if (argvars[2].tag != VAR_UNKNOWN
      && parse_diff_optarg(
            &argvars[2], &diff_flags, &diff_algorithm, &dio.dio_outfmt, OUT &dio.dio_ctxlen
         ) == FAIL
   ) 
      return;

   //Concatenate the List of strings into a single string using newline
   //separator.  Internal diff library expects a single string.
   list_to_diffin(orig_list, &dio.orig, diff_flags & DIFF_ICASE);
   list_to_diffin(new_list, &dio.new, diff_flags & DIFF_ICASE);

   //If @diffexpr is set, then the internal diff is not used.  Set
   //@diffexpr to an empty string temporarily.
   int restore_diffexpr = false;
   CS p_dexSaved = p_dex;
   if (p_dex) {
      restore_diffexpr = true;
      p_dex = null;
   }

   //Compute the diff
   int diff_status = diff_file(&dio);

   //restore @diffexpr
   if (restore_diffexpr)
      p_dex = p_dexSaved;

   if (diff_status == FAIL)
      goto done;
      
   int hunk_idx = 0;
   Bag* hunkBag;

   if (dio.dio_outfmt == DIO_OUTPUT_INDICES) {
      allocReturnList(returnVar);
      List* l = returnVar->list;

      // Process each diff hunk
      Hunk* hunk = NULL;
      while (hunk_idx < dio.dio_diff.dout_ga.len) {
         hunk = ((Hunk **)dio.dio_diff.dout_ga.c)[hunk_idx++];

         hunkBag = get_diff_hunk_indices(hunk);
         if (hunkBag == NULL)
            goto done;

         listAppendBag(l, hunkBag);
      }
   } else {
      ga_append(&dio.dio_diff.dout_ga, ZERO);
      returnVar->tag = VAR_STRING;
      returnVar->string = copyStr((CS)dio.dio_diff.dout_ga.c);
   }

done:
   clear_diffin(&dio.new);
   if (dio.dio_outfmt == DIO_OUTPUT_INDICES)
      clear_diffout(&dio.dio_diff);
   else
      ga_clear(&dio.dio_diff.dout_ga);
   clear_diffin(&dio.orig);
   // Restore the 'diffopt' option value.
   diff_flags = save_diff_flags;
   diff_algorithm = save_diff_algorithm;
}

//{{{xdiff (git diff algorithms)

// XdEmitConf.flags
#define XDL_EMIT_FUNCNAMES (1 << 0)
#define XDL_EMIT_NO_HUNK_HDR (1 << 1)
#define XDL_EMIT_FUNCCONTEXT (1 << 2)

// merge simplification levels
#define XDL_MERGE_MINIMAL 0
#define XDL_MERGE_EAGER 1
#define XDL_MERGE_ZEALOUS 2
#define XDL_MERGE_ZEALOUS_ALNUM 3

// merge favor modes
#define XDL_MERGE_FAVOR_OURS 1
#define XDL_MERGE_FAVOR_THEIRS 2
#define XDL_MERGE_FAVOR_UNION 3

// merge output styles
#define XDL_MERGE_DIFF3 1
#define XDL_MERGE_ZEALOUS_DIFF3 2


#define xdl_malloc(x) lalloc((x), true)
#define xdl_calloc(n, sz) lallocZeroed(n*sz, true)
#define xdl_realloc(ptr,x) eeRealloc((ptr),(x))

private void* xdl_mmfile_first(MmFile *mmf, long *size);
private long xdl_mmfile_size(MmFile *mmf);

#define DEFAULT_CONFLICT_MARKER_SIZE 7


typedef struct s_xdchange {
   struct s_xdchange *next;
   long i1, i2;
   long chg1, chg2;
   int ignore;
} XdChange;

typedef int (*emit_func_t)(XdfEnv *xe, XdChange *xscr, XdEmitCb *ecb,
            XdEmitConf const *xecfg);

private XdChange *xdl_get_hunk(XdChange **xscr, XdEmitConf const *xecfg);
private int xdl_emit_diff(XdfEnv *xe, XdChange *xscr, XdEmitCb *ecb, XdEmitConf const *xecfg);

typedef struct s_diffdata {
   long nrec;
   unsigned long const *ha;
   long *rindex;
   CS rchg;
} DiffData;

typedef struct s_xdg {
   long mxcost;
   long snake_cnt;
   long heur_min;
} Environment;


private int matching_chars(const MmFile *m1, const MmFile *m2);
private int xdl_recs_cmp(
   DiffData *dd1, long off1, long lim1, DiffData *dd2, long off2, long lim2,
   long *kvdf, long *kvdb, int need_min, Environment *xenv
);
private int xdl_do_diff(MmFile *mf1, MmFile *mf2, XpParam const *xpp, XdfEnv *xe);
private int xdl_change_compact(XdFile *xdf, XdFile *xdfo, long flags);
private int xdl_build_script(XdfEnv *xe, XdChange **xscr);
private void xdl_free_script(XdChange *xscr);
private int xdl_do_patience_diff(XpParam const *xpp, XdfEnv *env);
private int xdl_do_histogram_diff(XpParam const *xpp, XdfEnv *env);

#define XDL_MAX_COST_MIN 256
#define XDL_HEUR_MIN_COST 256
#define XDL_LINE_MAX (long)((1UL << (8 * sizeof(long) - 1)) - 1)
#define XDL_SNAKE_CNT 20
#define XDL_K_HEUR 4

typedef struct s_xdpsplit {
   long i1, i2;
   int min_lo, min_hi;
} xdpsplit_t;

private long xdl_bogosqrt(long n);
private int xdl_emit_diffrec(CS rec, long size, CS pre, long psize, XdEmitCb *ecb);
private int xdl_cha_init(ChaStore *cha, long isize, long icount);
private void xdl_cha_free(ChaStore *cha);
private void *xdl_cha_alloc(ChaStore *cha);
private long xdl_guess_lines(MmFile *mf, long sample);
private int xdl_blankline(CS line, long size, long flags);
private int xdl_recmatch(CS l1, long s1, CS l2, long s2, long flags);
private Ulong xdl_hash_record(Byte** data, Byte* top, long flags);
private Unt xdl_hashbits(unsigned int size);
private int xdl_num_out(Byte *out, long val);
private int xdl_emit_hunk_hdr(long s1, long c1, long s2, long c2,
            Byte* func, long funclen, XdEmitCb *ecb);
private int xdl_fall_back_diff(XdfEnv *diff_env, XpParam const *xpp,
             int line1, int count1, int line2, int count2);

// Do not call this function directly, use XDL_ALLOC_GROW instead
private void* xdl_alloc_grow_helper(void* p, Long nr, Long* alloc, Unt size);

private int xdl_prepare_env(MmFile *mf1, MmFile *mf2, XpParam const *xpp, XdfEnv *xe);
private void xdl_free_env(XdfEnv *xe);

//{{{macros

#define XDL_MIN(a, b) ((a) < (b) ? (a): (b))
#define XDL_MAX(a, b) ((a) > (b) ? (a): (b))
#define XDL_ABS(v) ((v) >= 0 ? (v): -(v))
#define XDL_ISDIGIT(c) ((c) >= '0' && (c) <= '9')
#define XDL_ISSPACE(c) (isspace((unsigned char)(c)))
#define XDL_ADDBITS(v,b)   ((v) + ((v) >> (b)))
#define XDL_MASKBITS(b)      ((1UL << (b)) - 1)
#define XDL_HASHLONG(v,b)   (XDL_ADDBITS((unsigned long)(v), b) & XDL_MASKBITS(b))
#define XDL_LE32_PUT(p, v) \
do { \
   unsigned char *__p = (unsigned char *) (p); \
   *__p++ = (unsigned char) (v); \
   *__p++ = (unsigned char) ((v) >> 8); \
   *__p++ = (unsigned char) ((v) >> 16); \
   *__p = (unsigned char) ((v) >> 24); \
} while (0)
#define XDL_LE32_GET(p, v) \
do { \
   unsigned char const *__p = (unsigned char const *) (p); \
   (v) = (unsigned long) __p[0] | ((unsigned long) __p[1]) << 8 | \
      ((unsigned long) __p[2]) << 16 | ((unsigned long) __p[3]) << 24; \
} while (0)

// Allocate an array of nr elements, return NULL on failure
#define XDL_ALLOC_ARRAY(p, nr)            \
   ((p) = SIZE_MAX / sizeof(*(p)) >= (Unt)(nr)   \
      ? xdl_malloc((nr) * sizeof(*(p)))   \
      : NULL)

//Ensure array p can accommodate at least nr elements, growing the
//array and updating alloc (which is the number of allocated
//elements) as necessary. Free p and return -1 on failure, 0 on success
#define XDL_ALLOC_GROW(p, nr, alloc)   \
   (-!((nr) <= (alloc) ||      \
       ((p) = xdl_alloc_grow_helper((p), (nr), &(alloc), sizeof(*(p))))))


//}}}

//See "An O(ND) Difference Algorithm and its Variations", by Eugene Myers.
//Basically considers a "box" (off1, off2, lim1, lim2) and scan from both
//the forward diagonal starting from (off1, off2) and the backward diagonal
//starting from (lim1, lim2). If the K values on the same diagonal crosses
//returns the furthest point of reach. We might encounter expensive edge cases
//using this algorithm, so a little bit of heuristic is needed to cut the
//search and to return a suboptimal point.
private long xdl_split(
   unsigned long const *ha1, long off1, long lim1, unsigned long const *ha2, long off2, long lim2,
   long *kvdf, long *kvdb, int need_min, xdpsplit_t *spl, Environment *xenv
) {
   long dmin = off1 - lim2, dmax = lim1 - off2;
   long fmid = off1 - off2, bmid = lim1 - lim2;
   long odd = (fmid - bmid) & 1;
   long fmin = fmid, fmax = fmid;
   long bmin = bmid, bmax = bmid;
   long ec, d, i1, i2, prev1, best, dd, v, k;

   //Set initial diagonal values for both forward and backward path.
   kvdf[fmid] = off1;
   kvdb[bmid] = lim1;

   for (ec = 1;; ec++) {
      int got_snake = 0;

      //We need to extend the diagonal "domain" by one. If the next
      //values exits the box boundaries we need to change it in the
      //opposite direction because (max - min) must be a power of two.
      //
      //Also we initialize the external K value to -1 so that we can
      //avoid extra conditions in the check inside the core loop.
      if (fmin > dmin)
         kvdf[--fmin - 1] = -1;
      else
         ++fmin;
      if (fmax < dmax)
         kvdf[++fmax + 1] = -1;
      else
         --fmax;

      for (d = fmax; d >= fmin; d -= 2) {
         if (kvdf[d - 1] >= kvdf[d + 1])
            i1 = kvdf[d - 1] + 1;
         else
            i1 = kvdf[d + 1];
         prev1 = i1;
         i2 = i1 - d;
         for (; i1 < lim1 && i2 < lim2 && ha1[i1] == ha2[i2]; i1++, i2++);
         if (i1 - prev1 > xenv->snake_cnt)
            got_snake = 1;
         kvdf[d] = i1;
         if (odd && bmin <= d && d <= bmax && kvdb[d] <= i1) {
            spl->i1 = i1;
            spl->i2 = i2;
            spl->min_lo = spl->min_hi = 1;
            return ec;
         }
      }

      //We need to extend the diagonal "domain" by one. If the next values exits the box 
      //boundaries we need to change it in the opposite direction because (max - min) must
      //be a power of two.
      //
      //Also we initialize the external K value to -1 so that we can avoid extra conditions in 
      //the check inside the core loop.
      if (bmin > dmin)
         kvdb[--bmin - 1] = XDL_LINE_MAX;
      else
         ++bmin;
      if (bmax < dmax)
         kvdb[++bmax + 1] = XDL_LINE_MAX;
      else
         --bmax;

      for (d = bmax; d >= bmin; d -= 2) {
         if (kvdb[d - 1] < kvdb[d + 1])
            i1 = kvdb[d - 1];
         else
            i1 = kvdb[d + 1] - 1;
         prev1 = i1;
         i2 = i1 - d;
         for (; i1 > off1 && i2 > off2 && ha1[i1 - 1] == ha2[i2 - 1]; i1--, i2--);
         if (prev1 - i1 > xenv->snake_cnt)
            got_snake = 1;
         kvdb[d] = i1;
         if (!odd && fmin <= d && d <= fmax && i1 <= kvdf[d]) {
            spl->i1 = i1;
            spl->i2 = i2;
            spl->min_lo = spl->min_hi = 1;
            return ec;
         }
      }

      if (need_min)
         continue;

      //If the edit cost is above the heuristic trigger and if we got a good snake, we sample 
      //current diagonals to see if some of them have reached an "interesting" path. Our measure 
      //is a function of the distance from the diagonal corner (i1 + i2) penalized with the 
      //distance from the mid diagonal itself. If this value is above the current
      //edit cost times a magic factor (XDL_K_HEUR) we consider it interesting.
      if (got_snake && ec > xenv->heur_min) {
         for (best = 0, d = fmax; d >= fmin; d -= 2) {
            dd = d > fmid ? d - fmid: fmid - d;
            i1 = kvdf[d];
            i2 = i1 - d;
            v = (i1 - off1) + (i2 - off2) - dd;

            if (v > XDL_K_HEUR * ec && v > best &&
                off1 + xenv->snake_cnt <= i1 && i1 < lim1 &&
                off2 + xenv->snake_cnt <= i2 && i2 < lim2) {
               for (k = 1; ha1[i1 - k] == ha2[i2 - k]; k++)
                  if (k == xenv->snake_cnt) {
                     best = v;
                     spl->i1 = i1;
                     spl->i2 = i2;
                     break;
                  }
            }
         }
         if (best > 0) {
            spl->min_lo = 1;
            spl->min_hi = 0;
            return ec;
         }

         for (best = 0, d = bmax; d >= bmin; d -= 2) {
            dd = d > bmid ? d - bmid: bmid - d;
            i1 = kvdb[d];
            i2 = i1 - d;
            v = (lim1 - i1) + (lim2 - i2) - dd;

            if (v > XDL_K_HEUR * ec && v > best 
                  && off1 < i1 && i1 <= lim1 - xenv->snake_cnt 
                  && off2 < i2 && i2 <= lim2 - xenv->snake_cnt
            ) {
               for (k = 0; ha1[i1 + k] == ha2[i2 + k]; k++) {
                  if (k == xenv->snake_cnt - 1) {
                     best = v;
                     spl->i1 = i1;
                     spl->i2 = i2;
                     break;
                  }
               } 
            }
         }
         if (best > 0) {
            spl->min_lo = 0;
            spl->min_hi = 1;
            return ec;
         }
      }

      //Enough is enough. We spent too much time here and now we
      //collect the furthest reaching path using the (i1 + i2) measure.
      if (ec >= xenv->mxcost) {
         long fbest, fbest1, bbest, bbest1;

         fbest = fbest1 = -1;
         for (d = fmax; d >= fmin; d -= 2) {
            i1 = XDL_MIN(kvdf[d], lim1);
            i2 = i1 - d;
            if (lim2 < i2)
               i1 = lim2 + d, i2 = lim2;
            if (fbest < i1 + i2) {
               fbest = i1 + i2;
               fbest1 = i1;
            }
         }

         bbest = bbest1 = XDL_LINE_MAX;
         for (d = bmax; d >= bmin; d -= 2) {
            i1 = XDL_MAX(off1, kvdb[d]);
            i2 = i1 - d;
            if (i2 < off2)
               i1 = off2 + d, i2 = off2;
            if (i1 + i2 < bbest) {
               bbest = i1 + i2;
               bbest1 = i1;
            }
         }

         if ((lim1 + lim2) - bbest < fbest - (off1 + off2)) {
            spl->i1 = fbest1;
            spl->i2 = fbest - fbest1;
            spl->min_lo = 1;
            spl->min_hi = 0;
         } else {
            spl->i1 = bbest1;
            spl->i2 = bbest - bbest1;
            spl->min_lo = 0;
            spl->min_hi = 1;
         }
         return ec;
      }
   }
}

//Rule: "Divide et Impera" (divide & conquer). Recursively split the box in
//sub-boxes by calling the box splitting function. Note that the real job
//(marking changed lines) is done in the two boundary reaching checks.
private int 
xdl_recs_cmp(DiffData* dd1, long off1, long lim1,
       DiffData* dd2, long off2, long lim2,
       long* kvdf, long* kvdb, int need_min, Environment* xenv
) {
   unsigned long const *ha1 = dd1->ha, *ha2 = dd2->ha;

   //Shrink the box by walking through each diagonal snake (SW and NE).
   for (; off1 < lim1 && off2 < lim2 && ha1[off1] == ha2[off2]; off1++, off2++);
   for (; off1 < lim1 && off2 < lim2 && ha1[lim1 - 1] == ha2[lim2 - 1]; lim1--, lim2--);

   //If one dimension is empty, then all records on the other one must be obviously changed.
   if (off1 == lim1) {
      CS rchg2 = dd2->rchg;
      long *rindex2 = dd2->rindex;

      for (; off2 < lim2; off2++)
         rchg2[rindex2[off2]] = 1;
   } ei (off2 == lim2) {
      Byte* rchg1 = dd1->rchg;
      long *rindex1 = dd1->rindex;

      for (; off1 < lim1; off1++)
         rchg1[rindex1[off1]] = 1;
   } else {
      xdpsplit_t spl;
      spl.i1 = spl.i2 = 0;

      //Divide ...
      if (xdl_split(ha1, off1, lim1, ha2, off2, lim2, kvdf, kvdb, need_min, &spl, xenv) < 0) {
         return -1;
      }

      //... et Impera.
      if (xdl_recs_cmp(dd1, off1, spl.i1, dd2, off2, spl.i2, kvdf, kvdb, spl.min_lo, xenv) < 0 
            || xdl_recs_cmp(dd1, spl.i1, lim1, dd2, spl.i2, lim2, kvdf, kvdb, spl.min_hi, xenv) < 0
      ) {
         return -1;
      }
   }

   return 0;
}

private int
xdl_do_diff(MmFile *mf1, MmFile *mf2, XpParam const *xpp, XdfEnv *xe) {
   Environment xenv;
   DiffData dd1, dd2;
   int res;

   if (xdl_prepare_env(mf1, mf2, xpp, xe) < 0)
      return -1;

   if (XDF_DIFF_ALG(xpp->flags) == PATIENCE_DIFF) {
      res = xdl_do_patience_diff(xpp, xe);
      goto out;
   }

   if (XDF_DIFF_ALG(xpp->flags) == XDF_HISTOGRAM_DIFF) {
      res = xdl_do_histogram_diff(xpp, xe);
      goto out;
   }

   //Allocate and setup K vectors to be used by the differential algorithm.
   //
   //One is to store the forward path and one to store the backward path.
   long ndiags = xe->xdf1.nreff + xe->xdf2.nreff + 3;
   long* kvd;
   if (!XDL_ALLOC_ARRAY(kvd, 2 * ndiags + 2)) {
      xdl_free_env(xe);
      return -1;
   }
   long* kvdf = kvd;
   long* kvdb = kvdf + ndiags;
   kvdf += xe->xdf2.nreff + 1;
   kvdb += xe->xdf2.nreff + 1;

   xenv.mxcost = xdl_bogosqrt(ndiags);
   if (xenv.mxcost < XDL_MAX_COST_MIN)
      xenv.mxcost = XDL_MAX_COST_MIN;
   xenv.snake_cnt = XDL_SNAKE_CNT;
   xenv.heur_min = XDL_HEUR_MIN_COST;

   dd1.nrec = xe->xdf1.nreff;
   dd1.ha = xe->xdf1.ha;
   dd1.rchg = xe->xdf1.rchg;
   dd1.rindex = xe->xdf1.rindex;
   dd2.nrec = xe->xdf2.nreff;
   dd2.ha = xe->xdf2.ha;
   dd2.rchg = xe->xdf2.rchg;
   dd2.rindex = xe->xdf2.rindex;

   res = xdl_recs_cmp(
      &dd1, 0, dd1.nrec, &dd2, 0, dd2.nrec, kvdf, kvdb, (xpp->flags & NEED_MINIMAL) != 0, &xenv
   );
   eeglFree(kvd);
 out:
   if (res < 0)
      xdl_free_env(xe);

   return res;
}


private XdChange* xdl_add_change(XdChange* xscr, long i1, long i2, long chg1, long chg2) {
   XdChange *xch;

   if (!(xch = (XdChange *) xdl_malloc(sizeof(XdChange))))
      return NULL;

   xch->next = xscr;
   xch->i1 = i1;
   xch->i2 = i2;
   xch->chg1 = chg1;
   xch->chg2 = chg2;
   xch->ignore = 0;

   return xch;
}

private int
recs_match(Record* rec1, Record* rec2) {
   return (rec1->ha == rec2->ha);
}

//If a line is indented more than this, xget_indent() just returns this value.
//This avoids having to do absurd amounts of work for data that are not
//human-readable text, and also ensures that the output of xget_indent fits within an int.
#define MAX_INDENT 200

//Return the amount of indentation of the specified line, treating TAB as 8 columns. 
//Return -1 if line is empty or contains only whitespace. Clamp the output value at MAX_INDENT.
private int
xget_indent(Record* rec) {
   int ret = 0;

   for (int i = 0; i < rec->size; i++) {
      Byte c = rec->ptr[i];

      if (!XDL_ISSPACE(c))
         return ret;
      ei (c == ' ')
         ret += 1;
      ei (c == '\t')
         ret += 8 - ret % 8;
      // ignore other whitespace characters

      if (ret >= MAX_INDENT)
         return MAX_INDENT;
   }

   // The line contains only whitespace
   return -1;
}

//If more than this number of consecutive blank rows are found, just return
//this value. This avoids requiring O(N^2) work for pathological cases, and
//also ensures that the output of score_split fits in an int.
#define MAX_BLANKS 20

// Characteristics measured about a hypothetical split position.
typedef struct SplitMeasurement {
   //Is the split at the end of the file (aside from any blank lines)?
   int end_of_file;

   //How much is the line immediately following the split indented (or -1 if the line is blank):
   int indent;

   //How many consecutive lines above the split are blank?
   int pre_blank;

   //How much is the nearest non-blank line above the split indented (or
   //-1 if there is no such line)?
   int pre_indent;

   //How many lines after the line following the split are blank?
   int post_blank;

   //How much is the nearest non-blank line after the line following the
   //split indented (or -1 if there is no such line)?
   int post_indent;
} SplitMeasurement;

typedef struct {
   // The effective indent of this split (smaller is preferred).
   int effective_indent;

   // Penalty for this split (smaller is preferred).
   int penalty;
} SplitScore;

//Fill m with information about a hypothetical split of xdf above line split.
private void 
measure_split(const XdFile* xdf, long split, SplitMeasurement* m) {
   long i;

   if (split >= xdf->nrec) {
      m->end_of_file = 1;
      m->indent = -1;
   } else {
      m->end_of_file = 0;
      m->indent = xget_indent(xdf->recs[split]);
   }

   m->pre_blank = 0;
   m->pre_indent = -1;
   for (i = split - 1; i >= 0; i--) {
      m->pre_indent = xget_indent(xdf->recs[i]);
      if (m->pre_indent != -1)
         break;
      m->pre_blank += 1;
      if (m->pre_blank == MAX_BLANKS) {
         m->pre_indent = 0;
         break;
      }
   }

   m->post_blank = 0;
   m->post_indent = -1;
   for (i = split + 1; i < xdf->nrec; i++) {
      m->post_indent = xget_indent(xdf->recs[i]);
      if (m->post_indent != -1)
         break;
      m->post_blank += 1;
      if (m->post_blank == MAX_BLANKS) {
         m->post_indent = 0;
         break;
      }
   }
}

//The empirically-determined weight factors used by score_split() below.
//Larger values means that the position is a less favorable place to split.
//
//Note that scores are only ever compared against each other, so multiplying
//all of these weight/penalty values by the same factor wouldn't change the
//heuristic's behavior. Still, we need to set that arbitrary scale *somehow*.
//In practice, these numbers are chosen to be large enough that they can be
//adjusted relative to each other with sufficient precision despite using integer math.

// Penalty if there are no non-blank lines before the split
#define START_OF_FILE_PENALTY 1

// Penalty if there are no non-blank lines after the split
#define END_OF_FILE_PENALTY 21

// Multiplier for the number of blank lines around the split
#define TOTAL_BLANK_WEIGHT (-30)

// Multiplier for the number of blank lines after the split
#define POST_BLANK_WEIGHT 6

//Penalties applied if the line is indented more than its predecessor
#define RELATIVE_INDENT_PENALTY (-4)
#define RELATIVE_INDENT_WITH_BLANK_PENALTY 10

//Penalties applied if the line is indented less than both its predecessor and its successor
#define RELATIVE_OUTDENT_PENALTY 24
#define RELATIVE_OUTDENT_WITH_BLANK_PENALTY 17

//Penalties applied if the line is indented less than its predecessor but no less than its successor
#define RELATIVE_DEDENT_PENALTY 23
#define RELATIVE_DEDENT_WITH_BLANK_PENALTY 17

//We only consider whether the sum of the effective indents for splits are
//less than (-1), equal to (0), or greater than (+1) each other. The resulting
//value is multiplied by the following weight and combined with the penalty to
//determine the better of two scores.
#define INDENT_WEIGHT 60

//How far do we slide a hunk at most?
#define INDENT_HEURISTIC_MAX_SLIDING 100

//Compute a badness score for the hypothetical split whose measurements are
//stored in m. The weight factors were determined empirically using the tools
//and corpus described in
//
//    https://github.com/mhagger/diff-slider-tools
//
//Also see that project if you want to improve the weights based on, for
//example, a larger or more diverse corpus.
private void
score_add_split(const SplitMeasurement* m, SplitScore* s) {
   //A place to accumulate penalty factors (positive makes this index more favored):
   int post_blank, total_blank, indent, any_blanks;

   if (m->pre_indent == -1 && m->pre_blank == 0)
      s->penalty += START_OF_FILE_PENALTY;

   if (m->end_of_file)
      s->penalty += END_OF_FILE_PENALTY;

   //Set post_blank to the number of blank lines following the split,
   //including the line immediately after the split:
   post_blank = (m->indent == -1) ? 1 + m->post_blank : 0;
   total_blank = m->pre_blank + post_blank;

   // Penalties based on nearby blank lines:
   s->penalty += TOTAL_BLANK_WEIGHT * total_blank;
   s->penalty += POST_BLANK_WEIGHT * post_blank;

   if (m->indent != -1)
      indent = m->indent;
   else
      indent = m->post_indent;

   any_blanks = (total_blank != 0);

   // Note that the effective indent is -1 at the end of the file:
   s->effective_indent += indent;

   if (indent == -1) {
      // No additional adjustments needed.
   } ei (m->pre_indent == -1) {
      // No additional adjustments needed.
   } ei (indent > m->pre_indent) {
      //The line is indented more than its predecessor.
      s->penalty += any_blanks ?
         RELATIVE_INDENT_WITH_BLANK_PENALTY :
         RELATIVE_INDENT_PENALTY;
   } ei (indent == m->pre_indent) {
      //The line has the same indentation level as its predecessor.
      //No additional adjustments needed.
   } else {
      //The line is indented less than its predecessor. It could be the block terminator of the 
      //previous block, but it could also be the start of a new block (e.g., an "else" block, or
      //maybe the previous block didn't have a block terminator). Try to distinguish those cases 
      //based on what comes next:
      if (m->post_indent != -1 && m->post_indent > indent) {
         //The following line is indented more. So it is likely
         //that this line is the start of a block.
         s->penalty += any_blanks ?
            RELATIVE_OUTDENT_WITH_BLANK_PENALTY :
            RELATIVE_OUTDENT_PENALTY;
      } else {
         //That was probably the end of a block.
         s->penalty += any_blanks ?
            RELATIVE_DEDENT_WITH_BLANK_PENALTY :
            RELATIVE_DEDENT_PENALTY;
      }
   }
}

private int
score_cmp(SplitScore* s1, SplitScore* s2) {
   // -1 if s1.effective_indent < s2->effective_indent, etc.
   int cmp_indents = ((s1->effective_indent > s2->effective_indent) 
         - (s1->effective_indent < s2->effective_indent));

   return INDENT_WEIGHT * cmp_indents + (s1->penalty - s2->penalty);
}

//Represent a group of changed lines in an XdFile (i.e., a contiguous group
//of lines that was inserted or deleted from the corresponding version of the
//file). We consider there to be such a group at the beginning of the file, at
//the end of the file, and between any two unchanged lines, though most such
//groups will usually be empty.
//
//If the first line in a group is equal to the line following the group, then
//the group can be slid down. Similarly, if the last line in a group is equal
//to the line preceding the group, then the group can be slid up. See
//group_slide_down() and group_slide_up().
//
//Note that loops that are testing for changed lines in xdf->rchg do not need
//index bounding since the array is prepared with a zero at position -1 and N.
typedef struct {
   //The index of the first changed line in the group, or the index of
   //the unchanged line above which the (empty) group is located.
   long start;

   //The index of the first unchanged line after the group. For an empty group, end == start
   long end;
} XdlGroup;

//Initialize g to point at the first group in xdf.
private void
group_init(XdFile* xdf, XdlGroup* g) {
   g->start = g->end = 0;
   while (xdf->rchg[g->end])
      g->end++;
}

//Move g to describe the next (possibly empty) group in xdf and return 0. If g
//is already at the end of the file, do nothing and return -1.
private inline int 
group_next(XdFile* xdf, XdlGroup* g) {
   if (g->end == xdf->nrec)
      return -1;

   g->start = g->end + 1;
   for (g->end = g->start; xdf->rchg[g->end]; g->end++)
      ;

   return 0;
}

//Move g to describe the previous (possibly empty) group in xdf and return 0.
//If g is already at the beginning of the file, do nothing and return -1.
private inline int
group_previous(XdFile* xdf, XdlGroup* g) {
   if (g->start == 0)
      return -1;

   g->end = g->start - 1;
   for (g->start = g->end; xdf->rchg[g->start - 1]; g->start--)
      ;

   return 0;
}

//If g can be slid toward the end of the file, do so, and if it bumps into a
//following group, expand this group to include it. Return 0 on success or -1
//if g cannot be slid down.
private int group_slide_down(XdFile* xdf, XdlGroup* g) {
   if (g->end < xdf->nrec && recs_match(xdf->recs[g->start], xdf->recs[g->end])) {
      xdf->rchg[g->start++] = 0;
      xdf->rchg[g->end++] = 1;

      while (xdf->rchg[g->end])
         g->end++;

      return 0;
   } else {
      return -1;
   }
}

//If g can be slid toward the beginning of the file, do so, and if it bumps into a previous group,
//expand this group to include it. Return 0 on success or -1 if g cannot be slid up.
private int
group_slide_up(XdFile* xdf, XdlGroup* g) {
   if (g->start > 0 && recs_match(xdf->recs[g->start - 1], xdf->recs[g->end - 1])) {
      xdf->rchg[--g->start] = 1;
      xdf->rchg[--g->end] = 0;

      while (xdf->rchg[g->start - 1])
         g->start--;

      return 0;
   } else {
      return -1;
   }
}

private void
xdl_bug(CS msg) {
   fprintf(stderr, "BUG: %s\n", msg);
   exit(1);
}

//Move back and forward change groups for a consistent and pretty diff output.
//This also helps in finding joinable change groups and reducing the diff size.
private int
xdl_change_compact(XdFile* xdf, XdFile* xdfo, long flags) {
   XdlGroup g, go;
   long earliest_end, end_matching_other;
   long groupsize;

   group_init(xdf, &g);
   group_init(xdfo, &go);

   while (1) {
      //If the group is empty in the to-be-compacted file, skip it:
      if (g.end == g.start)
         goto next;

      //Now shift the change up and then down as far as possible in
      //each direction. If it bumps into any other changes, merge them.
      do {
         groupsize = g.end - g.start;

         //Keep track of the last "end" index that causes this group to align with a group of 
         //changed lines in the other file. -1 indicates that we haven't found such a match yet:
         end_matching_other = -1;

         // Shift the group backward as much as possible:
         while (!group_slide_up(xdf, &g))
            if (group_previous(xdfo, &go))
               xdl_bug(S"group sync broken sliding up");

         //This is this highest that this group can be shifted. Record its end index:
         earliest_end = g.end;

         if (go.end > go.start)
            end_matching_other = g.end;

         // Now shift the group forward as far as possible:
         while (1) {
            if (group_slide_down(xdf, &g))
               break;
            if (group_next(xdfo, &go))
               xdl_bug(S"group sync broken sliding down");

            if (go.end > go.start)
               end_matching_other = g.end;
         }
      } while (groupsize != g.end - g.start);

      //If the group can be shifted, then we can possibly use this
      //freedom to produce a more intuitive diff.
      //
      //The group is currently shifted as far down as possible, so
      //the heuristics below only have to handle upwards shifts.

      if (g.end == earliest_end) {
         // no shifting was possible
      } ei (end_matching_other != -1) {
         //Move the possibly merged group of changes back to line up with the last group of 
         //changes from the other file that it can align with.
         while (go.end == go.start) {
            if (group_slide_up(xdf, &g))
               xdl_bug(S"match disappeared");
            if (group_previous(xdfo, &go))
               xdl_bug(S"group sync broken sliding to match");
         }
      } ei (flags & INDENT_HEURISTIC) {
         //Indent heuristic: a group of pure add/delete lines implies two splits, one between the 
         //end of the "before" context and the start of the group, and another between the end of
         //the group and the beginning of the "after" context. Some splits are aesthetically better
         //and some are worse. We compute a badness "score" for each split, and add the scores
         //for the two splits to define a "score" for each position that the group can be shifted 
         //to. Then we pick the shift with the lowest score.
         long shift, best_shift = -1;
         SplitScore best_score;

         shift = earliest_end;
         if (g.end - groupsize - 1 > shift)
            shift = g.end - groupsize - 1;
         if (g.end - INDENT_HEURISTIC_MAX_SLIDING > shift)
            shift = g.end - INDENT_HEURISTIC_MAX_SLIDING;
         for (; shift <= g.end; shift++) {
            SplitMeasurement m;
            SplitScore score = {0, 0};

            measure_split(xdf, shift, &m);
            score_add_split(&m, &score);
            measure_split(xdf, shift - groupsize, &m);
            score_add_split(&m, &score);
            if (best_shift == -1 || score_cmp(&score, &best_score) <= 0) {
               best_score.effective_indent = score.effective_indent;
               best_score.penalty = score.penalty;
               best_shift = shift;
            }
         }

         while (g.end > best_shift) {
            if (group_slide_up(xdf, &g))
               xdl_bug(S"best shift unreached");
            if (group_previous(xdfo, &go))
               xdl_bug(S"group sync broken sliding to blank line");
         }
      }

   next:
      // Move past the just-processed group:
      if (group_next(xdf, &g))
         break;
      if (group_next(xdfo, &go))
         xdl_bug(S"group sync broken moving to next group");
   }

   if (!group_next(xdfo, &go))
      xdl_bug(S"group sync broken at end of file");

   return 0;
}


private int
xdl_build_script(XdfEnv* xe, XdChange** xscr) {
   XdChange *cscr = NULL, *xch;
   Byte *rchg1 = xe->xdf1.rchg, *rchg2 = xe->xdf2.rchg;
   long i1, i2, l1, l2;

   //Trivial. Collects "groups" of changes and creates an edit script.
   for (i1 = xe->xdf1.nrec, i2 = xe->xdf2.nrec; i1 >= 0 || i2 >= 0; i1--, i2--) {
      if (rchg1[i1 - 1] || rchg2[i2 - 1]) {
         for (l1 = i1; rchg1[i1 - 1]; i1--);
         for (l2 = i2; rchg2[i2 - 1]; i2--);

         if (!(xch = xdl_add_change(cscr, i1, i2, l1 - i1, l2 - i2))) {
            xdl_free_script(cscr);
            return -1;
         }
         cscr = xch;
      }
   } 

   *xscr = cscr;

   return 0;
}


private void
xdl_free_script(XdChange* xscr) {
   XdChange* xch;
   while ((xch = xscr) != NULL) {
      xscr = xscr->next;
      eeglFree(xch);
   }
}

private int 
xdl_call_hunk_func(
      XdfEnv* xe UNUSED, XdChange* xscr, XdEmitCb* ecb, XdEmitConf const* xecfg
) {
   XdChange *xch, *xche;

   for (xch = xscr; xch; xch = xche->next) {
      xche = xdl_get_hunk(&xch, xecfg);
      if (!xch)
         break;
      if (xecfg->hunk_func(xch->i1, xche->i1 + xche->chg1 - xch->i1,
                 xch->i2, xche->i2 + xche->chg2 - xch->i2,
                 ecb->priv) < 0)
         return -1;
   }
   return 0;
}

private void 
xdl_mark_ignorable_lines(XdChange *xscr, XdfEnv *xe, long flags) {
   for (XdChange* xch = xscr; xch; xch = xch->next) {
      int ignore = 1;
      Record **rec;
      long i;

      rec = &xe->xdf1.recs[xch->i1];
      for (i = 0; i < xch->chg1 && ignore; i++)
         ignore = xdl_blankline(rec[i]->ptr, rec[i]->size, flags);

      rec = &xe->xdf2.recs[xch->i2];
      for (i = 0; i < xch->chg2 && ignore; i++)
         ignore = xdl_blankline(rec[i]->ptr, rec[i]->size, flags);

      xch->ignore = ignore;
   }
}

private int
xdl_diff(MmFile* mf1, MmFile* mf2, XpParam* xpp, XdEmitConf* xecfg, XdEmitCb* ecb) {
   XdChange *xscr;
   XdfEnv xe;
   emit_func_t ef = xecfg->hunk_func ? xdl_call_hunk_func : xdl_emit_diff;

   if (xdl_do_diff(mf1, mf2, xpp, &xe) < 0) {

      return -1;
   }
   if (xdl_change_compact(&xe.xdf1, &xe.xdf2, xpp->flags) < 0 ||
       xdl_change_compact(&xe.xdf2, &xe.xdf1, xpp->flags) < 0 ||
       xdl_build_script(&xe, &xscr) < 0) {

      xdl_free_env(&xe);
      return -1;
   }
   if (xscr) {
      if (xpp->flags & IGNORE_BLANK_LINES)
         xdl_mark_ignorable_lines(xscr, &xe, xpp->flags);

      if (ef(&xe, xscr, ecb, xecfg) < 0) {
         xdl_free_script(xscr);
         xdl_free_env(&xe);
         return -1;
      }
      xdl_free_script(xscr);
   }
   xdl_free_env(&xe);

   return 0;
}

private long
xdl_bogosqrt(long n) {
   long i;

   //Classical integer square root approximation using shifts.
   for (i = 1; n > 0; n >>= 2)
      i <<= 1;

   return i;
}

private int
xdl_emit_diffrec(CS rec, long size, CS pre, long psize, XdEmitCb* ecb) {
   int i = 2;
   MmBuffer mb[3];

   mb[0].ptr = pre;
   mb[0].size = psize;
   mb[1].ptr = rec;
   mb[1].size = size;
   if (size > 0 && rec[size - 1] != '\n') {
      mb[2].ptr = S"\n\\ No newline at end of file\n";
      mb[2].size = (long)STRLEN(mb[2].ptr);
      i++;
   }
   if (ecb->out_line(ecb->priv, mb, i) < 0) {
      return -1;
   }

   return 0;
}

private void*
xdl_mmfile_first(MmFile* mmf, long* size) {
   *size = mmf->size;
   return mmf->ptr;
}


private long
xdl_mmfile_size(MmFile* mmf) {
   return mmf->size;
}


private int
xdl_cha_init(ChaStore* cha, long isize, long icount) {
   cha->head = cha->tail = NULL;
   cha->isize = isize;
   cha->nsize = icount * isize;
   cha->ancur = cha->sncur = NULL;
   cha->scurr = 0;

   return 0;
}

private void
xdl_cha_free(ChaStore* cha) {
   ChaNode *cur, *tmp;

   for (cur = cha->head; (tmp = cur) != NULL;) {
      cur = cur->next;
      eeglFree(tmp);
   }
}


private void*
xdl_cha_alloc(ChaStore* cha) {
   ChaNode* ancur;
   void* data;

   if (!(ancur = cha->ancur) || ancur->icurr == cha->nsize) {
      if (!(ancur = (ChaNode *) xdl_malloc(sizeof(ChaNode) + cha->nsize))) {

         return NULL;
      }
      ancur->icurr = 0;
      ancur->next = NULL;
      if (cha->tail)
         cha->tail->next = ancur;
      if (!cha->head)
         cha->head = ancur;
      cha->tail = ancur;
      cha->ancur = ancur;
   }

   data = (char *) ancur + sizeof(ChaNode) + ancur->icurr;
   ancur->icurr += cha->isize;

   return data;
}

private long
xdl_guess_lines(MmFile* mf, long sample) {
   long nl = 0, size, tsize = 0;
   char const *data, *cur, *top;

   if ((cur = data = xdl_mmfile_first(mf, &size))) {
      for (top = data + size; nl < sample && cur < top; ) {
         nl++;
         if (!(cur = memchr(cur, '\n', top - cur)))
            cur = top;
         else
            cur++;
      }
      tsize += (long) (cur - data);
   }

   if (nl && tsize)
      nl = xdl_mmfile_size(mf) / (tsize / nl);

   return nl + 1;
}

private int
xdl_blankline(CS line, long size, long flags) {
   if ((flags & WHITESPACE_FLAGS) == 0)
      return (size <= 1);

   long i;
   for (i = 0; i < size && XDL_ISSPACE(line[i]); i++)
      ;

   return (i == size);
}

//Have we eaten everything on the line, except for an optional CR at the very end?
private int
ends_with_optional_cr(CS l, long s, long i) {
   int complete = s && l[s - 1] == '\n';

   if (complete)
      s--;
   if (s == i)
      return 1;
   /* do not ignore CR at the end of an incomplete line */
   if (complete && s == i + 1 && l[i] == '\r')
      return 1;
   return 0;
}

private int
xdl_recmatch(CS l1, long s1, CS l2, long s2, long flags) {
   int i1, i2;

   if (s1 == s2 && !memcmp(l1, l2, s1))
      return 1;
   if (!(flags & WHITESPACE_FLAGS))
      return 0;

   i1 = 0;
   i2 = 0;

   //-w matches everything that matches with -b, and -b in turn
   //matches everything that matches with --ignore-space-at-eol,
   //which in turn matches everything that matches with --ignore-cr-at-eol.
   //
   //Each flavor of ignoring needs different logic to skip whitespaces
   //while we have both sides to compare.
   if ((flags & IGNORE_WHITESPACE) != 0) {
      goto skip_ws;
      while (i1 < s1 && i2 < s2) {
         if (l1[i1++] != l2[i2++])
            return 0;
      skip_ws:
         while (i1 < s1 && XDL_ISSPACE(l1[i1]))
            i1++;
         while (i2 < s2 && XDL_ISSPACE(l2[i2]))
            i2++;
      }
   } ei (flags & IGNORE_WHITESPACE_CHANGE) {
      while (i1 < s1 && i2 < s2) {
         if (XDL_ISSPACE(l1[i1]) && XDL_ISSPACE(l2[i2])) {
            // Skip matching spaces and try again
            while (i1 < s1 && XDL_ISSPACE(l1[i1]))
               i1++;
            while (i2 < s2 && XDL_ISSPACE(l2[i2]))
               i2++;
            continue;
         }
         if (l1[i1++] != l2[i2++])
            return 0;
      }
   } ei (flags & IGNORE_WHITESPACE_AT_EOL) {
      while (i1 < s1 && i2 < s2 && l1[i1] == l2[i2]) {
         i1++;
         i2++;
      }
   } ei (flags & IGNORE_CR_AT_EOL) {
      // Find the first difference and see how the line ends
      while (i1 < s1 && i2 < s2 && l1[i1] == l2[i2]) {
         i1++;
         i2++;
      }
      return (ends_with_optional_cr(l1, s1, i1) &&
         ends_with_optional_cr(l2, s2, i2));
   }

   //After running out of one side, the remaining side must have nothing but whitespace for the 
   //lines to match. Note that ignore-whitespace-at-eol case may break out of the loop
   //while there still are characters remaining on both lines.
   if (i1 < s1) {
      while (i1 < s1 && XDL_ISSPACE(l1[i1]))
         i1++;
      if (s1 != i1)
         return 0;
   }
   if (i2 < s2) {
      while (i2 < s2 && XDL_ISSPACE(l2[i2]))
         i2++;
      return (s2 == i2);
   }
   return 1;
}

private unsigned long xdl_hash_record_with_whitespace(
   Byte** data, CS top, long flags
) {
   unsigned long ha = 5381;
   CS ptr = *data;
   int cr_at_eol_only = (flags & WHITESPACE_FLAGS) == IGNORE_CR_AT_EOL;

   for (; ptr < top && *ptr != '\n'; ptr++) {
      if (cr_at_eol_only) {
         // do not ignore CR at the end of an incomplete line
         if (*ptr == '\r' && (ptr + 1 < top && ptr[1] == '\n'))
            continue;
      } ei (XDL_ISSPACE(*ptr)) {
         CS ptr2 = ptr;
         int at_eol;
         while (ptr + 1 < top && XDL_ISSPACE(ptr[1]) && ptr[1] != '\n')
            ptr++;
         at_eol = (top <= ptr + 1 || ptr[1] == '\n');
         if (flags & IGNORE_WHITESPACE)
            ; // already handled
         ei (flags & IGNORE_WHITESPACE_CHANGE && !at_eol) {
            ha += (ha << 5);
            ha ^= (unsigned long) ' ';
         } ei (flags & IGNORE_WHITESPACE_AT_EOL && !at_eol) {
            while (ptr2 != ptr + 1) {
               ha += (ha << 5);
               ha ^= (unsigned long) *ptr2;
               ptr2++;
            }
         }
         continue;
      }
      ha += (ha << 5);
      ha ^= (unsigned long) *ptr;
   }
   *data = ptr < top ? ptr + 1: ptr;

   return ha;
}

private Ulong
xdl_hash_record(Byte** data, CS top, long flags) {
   unsigned long ha = 5381;
   CS ptr = *data;

   if ((flags & WHITESPACE_FLAGS) != 0)
      return xdl_hash_record_with_whitespace(data, top, flags);

   for (; ptr < top && *ptr != '\n'; ptr++) {
      ha += (ha << 5);
      ha ^= (unsigned long) *ptr;
   }
   *data = ptr < top ? ptr + 1: ptr;

   return ha;
}

private Unt
xdl_hashbits(Unt size) {
   Unt val = 1, bits = 0;

   for (; val < size && bits < 8 * sizeof(unsigned int); val <<= 1, bits++);
   return bits ? bits: 1;
}

private int 
xdl_num_out(CS out, long val) {
   CS str = out;
   Byte buf[32];

   CS ptr = buf + sizeof(buf) - 1;
   *ptr = '\0';
   if (val < 0) {
      *--ptr = '-';
      val = -val;
   }
   for (; val && ptr > buf; val /= 10)
      *--ptr = "0123456789"[val % 10];
   if (*ptr)
      for (; *ptr; ptr++, str++)
         *str = *ptr;
   else
      *str++ = '0';
   *str = '\0';

   return str - out;
}

private int xdl_format_hunk_hdr(
   long s1, long c1, long s2, long c2, CS func, long funclen, XdEmitCb* ecb
) {
   MmBuffer mb;
   Byte buf[128];

   memcpy(buf, "@@ -", 4);
   int nb = 4;
   nb += xdl_num_out(buf + nb, c1 ? s1: s1 - 1);

   if (c1 != 1) {
      memcpy(buf + nb, ",", 1);
      nb += 1;

      nb += xdl_num_out(buf + nb, c1);
   }

   memcpy(buf + nb, " +", 2);
   nb += 2;

   nb += xdl_num_out(buf + nb, c2 ? s2: s2 - 1);

   if (c2 != 1) {
      memcpy(buf + nb, ",", 1);
      nb += 1;
      nb += xdl_num_out(buf + nb, c2);
   }

   memcpy(buf + nb, " @@", 3);
   nb += 3;
   if (func && funclen) {
      buf[nb++] = ' ';
      if (funclen > (long)sizeof(buf) - nb - 1)
         funclen = sizeof(buf) - nb - 1;
      memcpy(buf + nb, func, funclen);
      nb += funclen;
   }
   buf[nb++] = '\n';

   mb.ptr = buf;
   mb.size = nb;
   if (ecb->out_line(ecb->priv, &mb, 1) < 0)
      return -1;
   return 0;
}

private int
xdl_emit_hunk_hdr(
   long s1, long c1, long s2, long c2, Byte* func, long funclen, XdEmitCb *ecb
) {
   if (!ecb->out_hunk)
      return xdl_format_hunk_hdr(s1, c1, s2, c2, func, funclen, ecb);
   if (ecb->out_hunk(
         ecb->priv, c1 ? s1 : s1 - 1, c1, c2 ? s2 : s2 - 1, c2, func, funclen) < 0
   )
      return -1;
   return 0;
}

//This probably does not work outside Git, since we have a very simple mmfile structure.
//
//Note: ideally, we would reuse the prepared environment, but the libxdiff interface does not (yet)
//allow for diffing only ranges of lines instead of the whole files.
private int
xdl_fall_back_diff(
   XdfEnv* diff_env, XpParam const* xpp, int line1, int count1, int line2, int count2
) {
   MmFile subfile1, subfile2;
   XdfEnv env;

   subfile1.ptr = diff_env->xdf1.recs[line1 - 1]->ptr;
   subfile1.size = diff_env->xdf1.recs[line1 + count1 - 2]->ptr +
      diff_env->xdf1.recs[line1 + count1 - 2]->size - subfile1.ptr;
   subfile2.ptr = diff_env->xdf2.recs[line2 - 1]->ptr;
   subfile2.size = diff_env->xdf2.recs[line2 + count2 - 2]->ptr +
      diff_env->xdf2.recs[line2 + count2 - 2]->size - subfile2.ptr;
   if (xdl_do_diff(&subfile1, &subfile2, xpp, &env) < 0)
      return -1;

   memcpy(diff_env->xdf1.rchg + line1 - 1, env.xdf1.rchg, count1);
   memcpy(diff_env->xdf2.rchg + line2 - 1, env.xdf2.rchg, count2);

   xdl_free_env(&env);

   return 0;
}

private void*
xdl_alloc_grow_helper(void* p, Long nr, Long* alloc, Unt size) {
   void* tmp = NULL;
   Unt n = (*alloc <= ((Long)LONG_MAX - 16) / 2) ? (2*(*alloc) + 16) : UNT;
   if (nr > (long)n)
      n = nr;
   if (SIZE_MAX / size >= n)
      tmp = xdl_realloc(p, n * size);
   if (tmp) {
      *alloc = (long)n;
   } else {
      eeglFree(p);
      *alloc = 0;
   }
   return tmp;
}


#define XDL_KPDIS_RUN 4
#define XDL_MAX_EQLIMIT 1024
#define XDL_SIMSCAN_WINDOW 100
#define XDL_GUESS_NLINES1 256
#define XDL_GUESS_NLINES2 20

declStruct(XdlClass);
struct XdlClass {
   XdlClass* next;
   Ulong ha;
   CS line;
   long size;
   long idx;
   long len1;
   long len2;
};

typedef struct s_xdlclassifier {
   unsigned int hbits;
   long hsize;
   XdlClass **rchash;
   ChaStore ncha;
   XdlClass **rcrecs;
   long alloc;
   long count;
   long flags;
} Classifier;


private int xdl_init_classifier(Classifier *cf, long size, long flags);
private void xdl_free_classifier(Classifier *cf);
private int xdl_classify_record(unsigned int pass, Classifier *cf, Record **rhash,
                unsigned int hbits, Record *rec);
private int xdl_prepare_ctx(unsigned int pass, MmFile *mf, long narec, XpParam const *xpp,
            Classifier *cf, XdFile *xdf);
private void xdl_free_ctx(XdFile *xdf);
private int xdl_clean_mmatch(Byte* dis, long i, long s, long e);
private int xdl_cleanup_records(Classifier *cf, XdFile *xdf1, XdFile *xdf2);
private int xdl_trim_ends(XdFile *xdf1, XdFile *xdf2);
private int xdl_optimize_ctxs(Classifier *cf, XdFile *xdf1, XdFile *xdf2);


private int xdl_init_classifier(Classifier *cf, long size, long flags) {
   cf->flags = flags;

   cf->hbits = xdl_hashbits((unsigned int) size);
   cf->hsize = 1 << cf->hbits;

   if (xdl_cha_init(&cf->ncha, sizeof(XdlClass), size / 4 + 1) < 0) {
      return -1;
   }
   if (!XDL_CALLOC_ARRAY(cf->rchash, cf->hsize)) {
      xdl_cha_free(&cf->ncha);
      return -1;
   }

   cf->alloc = size;
   if (!XDL_ALLOC_ARRAY(cf->rcrecs, cf->alloc)) {
      eeglFree(cf->rchash);
      xdl_cha_free(&cf->ncha);
      return -1;
   }

   cf->count = 0;

   return 0;
}


private void xdl_free_classifier(Classifier *cf) {
   eeglFree(cf->rcrecs);
   eeglFree(cf->rchash);
   xdl_cha_free(&cf->ncha);
}


private int xdl_classify_record(unsigned int pass, Classifier *cf, Record **rhash,
                unsigned int hbits, Record *rec) {
   XdlClass *rcrec;

   CS line = rec->ptr;
   long hi = (long) XDL_HASHLONG(rec->ha, cf->hbits);
   for (rcrec = cf->rchash[hi]; rcrec; rcrec = rcrec->next) {
      if (rcrec->ha == rec->ha 
            && xdl_recmatch(rcrec->line, rcrec->size, rec->ptr, rec->size, cf->flags)
      )
         break;
   } 

   if (!rcrec) {
      if (!(rcrec = xdl_cha_alloc(&cf->ncha))) {
         return -1;
      }
      rcrec->idx = cf->count++;
      if (XDL_ALLOC_GROW(cf->rcrecs, cf->count, cf->alloc))
            return -1;
      cf->rcrecs[rcrec->idx] = rcrec;
      rcrec->line = line;
      rcrec->size = rec->size;
      rcrec->ha = rec->ha;
      rcrec->len1 = rcrec->len2 = 0;
      rcrec->next = cf->rchash[hi];
      cf->rchash[hi] = rcrec;
   }

   (pass == 1) ? rcrec->len1++ : rcrec->len2++;

   rec->ha = (unsigned long) rcrec->idx;

   hi = (long) XDL_HASHLONG(rec->ha, hbits);
   rec->next = rhash[hi];
   rhash[hi] = rec;

   return 0;
}


private int xdl_prepare_ctx(unsigned int pass, MmFile *mf, long narec, XpParam const *xpp,
            Classifier *cf, XdFile *xdf
) {
   long nrec, hsize, bsize;
   unsigned long hav;
   Byte *blk, *cur, *top, *prev;
   Record *crec;

   Ulong* ha = NULL;
   long* rindex = NULL;
   Byte* rchg = NULL;
   Record** rhash = NULL;
   Record** recs = NULL;

   if (xdl_cha_init(&xdf->rcha, sizeof(Record), narec / 4 + 1) < 0)
      goto abort;
   if (!XDL_ALLOC_ARRAY(recs, narec))
      goto abort;

   Unt hbits = xdl_hashbits((unsigned int) narec);
   hsize = 1 << hbits;
   if (!XDL_CALLOC_ARRAY(rhash, hsize))
      goto abort;

   nrec = 0;
   if ((cur = blk = xdl_mmfile_first(mf, &bsize))) {
      for (top = blk + bsize; cur < top; ) {
         prev = cur;
         hav = xdl_hash_record(&cur, top, xpp->flags);
         if (XDL_ALLOC_GROW(recs, nrec + 1, narec))
            goto abort;
         if (!(crec = xdl_cha_alloc(&xdf->rcha)))
            goto abort;
         crec->ptr = prev;
         crec->size = (long) (cur - prev);
         crec->ha = hav;
         recs[nrec++] = crec;
         if (xdl_classify_record(pass, cf, rhash, hbits, crec) < 0)
            goto abort;
      }
   }

   if (!XDL_CALLOC_ARRAY(rchg, nrec + 2))
      goto abort;

   if ((XDF_DIFF_ALG(xpp->flags) != PATIENCE_DIFF) &&
       (XDF_DIFF_ALG(xpp->flags) != XDF_HISTOGRAM_DIFF)) {
      if (!XDL_ALLOC_ARRAY(rindex, nrec + 1))
         goto abort;
      if (!XDL_ALLOC_ARRAY(ha, nrec + 1))
         goto abort;
   }

   xdf->nrec = nrec;
   xdf->recs = recs;
   xdf->hbits = hbits;
   xdf->rhash = rhash;
   xdf->rchg = rchg + 1;
   xdf->rindex = rindex;
   xdf->nreff = 0;
   xdf->ha = ha;
   xdf->dstart = 0;
   xdf->dend = nrec - 1;

   return 0;

abort:
   eeglFree(ha);
   eeglFree(rindex);
   eeglFree(rchg);
   eeglFree(rhash);
   eeglFree(recs);
   xdl_cha_free(&xdf->rcha);
   return -1;
}

private void xdl_free_ctx(XdFile *xdf) {
   eeglFree(xdf->rhash);
   eeglFree(xdf->rindex);
   eeglFree(xdf->rchg - 1);
   eeglFree(xdf->ha);
   eeglFree(xdf->recs);
   xdl_cha_free(&xdf->rcha);
}


private int
xdl_prepare_env(MmFile *mf1, MmFile *mf2, XpParam const *xpp, XdfEnv *xe) {
   long enl1, enl2, sample;
   Classifier cf;

   memset(&cf, 0, sizeof(cf));

   //For histogram diff, we can afford a smaller sample size and thus a poorer estimate of the 
   //number of lines, as the hash table (rhash) won't be filled up/grown. The number of lines
   //(nrecs) will be updated correctly anyway by xdl_prepare_ctx().
   sample = (XDF_DIFF_ALG(xpp->flags) == XDF_HISTOGRAM_DIFF
        ? XDL_GUESS_NLINES2 : XDL_GUESS_NLINES1);

   enl1 = xdl_guess_lines(mf1, sample) + 1;
   enl2 = xdl_guess_lines(mf2, sample) + 1;

   if (xdl_init_classifier(&cf, enl1 + enl2 + 1, xpp->flags) < 0)
      return -1;

   if (xdl_prepare_ctx(1, mf1, enl1, xpp, &cf, &xe->xdf1) < 0) {

      xdl_free_classifier(&cf);
      return -1;
   }
   if (xdl_prepare_ctx(2, mf2, enl2, xpp, &cf, &xe->xdf2) < 0) {
      xdl_free_ctx(&xe->xdf1);
      xdl_free_classifier(&cf);
      return -1;
   }

   if ((XDF_DIFF_ALG(xpp->flags) != PATIENCE_DIFF) &&
       (XDF_DIFF_ALG(xpp->flags) != XDF_HISTOGRAM_DIFF) &&
       xdl_optimize_ctxs(&cf, &xe->xdf1, &xe->xdf2) < 0
   ) {
      xdl_free_ctx(&xe->xdf2);
      xdl_free_ctx(&xe->xdf1);
      xdl_free_classifier(&cf);
      return -1;
   }

   xdl_free_classifier(&cf);

   return 0;
}

private void
xdl_free_env(XdfEnv *xe) {
   xdl_free_ctx(&xe->xdf2);
   xdl_free_ctx(&xe->xdf1);
}


private int 
xdl_clean_mmatch(Byte* dis, long i, long s, long e) {
   long r, rdis0, rpdis0, rdis1, rpdis1;

   //Limits the portal that is examined during the similar-lines scan. The loops below stops when 
   //dis[i - r] == 1 (line that has no match), but there are corner cases where the loop
   //proceed all the way to the extremities by causing huge performance penalties in case of big 
   //files.
   if (i - s > XDL_SIMSCAN_WINDOW)
      s = i - XDL_SIMSCAN_WINDOW;
   if (e - i > XDL_SIMSCAN_WINDOW)
      e = i + XDL_SIMSCAN_WINDOW;

   //Scans the lines before 'i' to find a run of lines that either have no match (dis[j] == 0) or 
   //have multiple matches (dis[j] > 1). Note that we always call this function with dis[i] > 1, 
   //so the current line (i) is already a multimatch line.
   for (r = 1, rdis0 = 0, rpdis0 = 1; (i - r) >= s; r++) {
      if (!dis[i - r])
         rdis0++;
      ei (dis[i - r] == 2)
         rpdis0++;
      else
         break;
   }
   //If the run before the line 'i' found only multimatch lines, we return 0 and hence we don't 
   //make the current line (i) discarded. We want to discard multimatch lines only when they 
   //appear in the middle of runs with nomatch lines (dis[j] == 0).
   if (rdis0 == 0)
      return 0;
   for (r = 1, rdis1 = 0, rpdis1 = 1; (i + r) <= e; r++) {
      if (!dis[i + r])
         rdis1++;
      ei (dis[i + r] == 2)
         rpdis1++;
      else
         break;
   }
   //If the run after the line 'i' found only multimatch lines, we
   //return 0 and hence we don't make the current line (i) discarded.
   if (rdis1 == 0)
      return 0;
   rdis1 += rdis0;
   rpdis1 += rpdis0;

   return rpdis1 * XDL_KPDIS_RUN < (rpdis1 + rdis1);
}

//Try to reduce the problem complexity, discard records that have no matches on the other file.
//Also, lines that have multiple matches might be potentially discarded if they happear in a run 
//of discardable.
private int xdl_cleanup_records(Classifier *cf, XdFile *xdf1, XdFile *xdf2) {
   long i, nm, nreff, mlim;
   Record **recs;
   XdlClass* rcrec;
   Byte *dis, *dis1, *dis2;

   if (!XDL_CALLOC_ARRAY(dis, xdf1->nrec + xdf2->nrec + 2))
      return -1;
   dis1 = dis;
   dis2 = dis1 + xdf1->nrec + 1;

   if ((mlim = xdl_bogosqrt(xdf1->nrec)) > XDL_MAX_EQLIMIT)
      mlim = XDL_MAX_EQLIMIT;
   for (i = xdf1->dstart, recs = &xdf1->recs[xdf1->dstart]; i <= xdf1->dend; i++, recs++) {
      rcrec = cf->rcrecs[(*recs)->ha];
      nm = rcrec ? rcrec->len2 : 0;
      dis1[i] = (nm == 0) ? 0: (nm >= mlim) ? 2: 1;
   }

   if ((mlim = xdl_bogosqrt(xdf2->nrec)) > XDL_MAX_EQLIMIT)
      mlim = XDL_MAX_EQLIMIT;
   for (i = xdf2->dstart, recs = &xdf2->recs[xdf2->dstart]; i <= xdf2->dend; i++, recs++) {
      rcrec = cf->rcrecs[(*recs)->ha];
      nm = rcrec ? rcrec->len1 : 0;
      dis2[i] = (nm == 0) ? 0: (nm >= mlim) ? 2: 1;
   }

   for (nreff = 0, i = xdf1->dstart, recs = &xdf1->recs[xdf1->dstart];
        i <= xdf1->dend; i++, recs++
   ) {
      if (dis1[i] == 1 || (dis1[i] == 2 && !xdl_clean_mmatch(dis1, i, xdf1->dstart, xdf1->dend))) {
         xdf1->rindex[nreff] = i;
         xdf1->ha[nreff] = (*recs)->ha;
         nreff++;
      } else
         xdf1->rchg[i] = 1;
   }
   xdf1->nreff = nreff;

   for (nreff = 0, i = xdf2->dstart, recs = &xdf2->recs[xdf2->dstart];
        i <= xdf2->dend; i++, recs++) {
      if (dis2[i] == 1 ||
          (dis2[i] == 2 && !xdl_clean_mmatch(dis2, i, xdf2->dstart, xdf2->dend))) {
         xdf2->rindex[nreff] = i;
         xdf2->ha[nreff] = (*recs)->ha;
         nreff++;
      } else
         xdf2->rchg[i] = 1;
   }
   xdf2->nreff = nreff;

   eeglFree(dis);

   return 0;
}

//Early trim initial and terminal matching records.
private int xdl_trim_ends(XdFile *xdf1, XdFile *xdf2) {
   long i, lim;
   Record **recs1, **recs2;

   recs1 = xdf1->recs;
   recs2 = xdf2->recs;
   for (i = 0, lim = XDL_MIN(xdf1->nrec, xdf2->nrec); i < lim;
        i++, recs1++, recs2++)
      if ((*recs1)->ha != (*recs2)->ha)
         break;

   xdf1->dstart = xdf2->dstart = i;

   recs1 = xdf1->recs + xdf1->nrec - 1;
   recs2 = xdf2->recs + xdf2->nrec - 1;
   for (lim -= i, i = 0; i < lim; i++, recs1--, recs2--)
      if ((*recs1)->ha != (*recs2)->ha)
         break;

   xdf1->dend = xdf1->nrec - i - 1;
   xdf2->dend = xdf2->nrec - i - 1;

   return 0;
}


private int xdl_optimize_ctxs(Classifier* cf, XdFile* xdf1, XdFile* xdf2) {
   if (xdl_trim_ends(xdf1, xdf2) < 0 ||
      xdl_cleanup_records(cf, xdf1, xdf2) < 0) {

      return -1;
   }

   return 0;
}


//{{{patience diff

//The basic idea of patience diff is to find lines that are unique in
//both files.  These are intuitively the ones that we want to see as common lines.
//
//The maximal ordered sequence of such line pairs (where ordered means
//that the order in the sequence agrees with the order of the lines in
//both files) naturally defines an initial set of common lines.
//
//Now, the algorithm tries to extend the set of common lines by growing
//the line ranges where the files have identical lines.
//
//Between those common lines, the patience diff algorithm is applied
//recursively, until no unique line pairs can be found; these line ranges
//are handled by the well-known Myers algorithm.

#define NON_UNIQUE UNT

//This is a hash mapping from line hash to line numbers in the first and second file.
declStruct(Entry);
struct Entry {
   Ulong hash;
   //0 = unused entry, 1 = first line, 2 = second, etc.
   //line2 is NON_UNIQUE if the line is not unique in either the first or the second file.
   Ulong line1;
   Ulong line2;
   //"next" & "previous" are used for the longest common sequence;
   //initially, "next" reflects only the order in file1.
   Entry* next;
   Entry* previous;

   //If 1, this entry can serve as an anchor. See manual/diff-options.txt for more information.
   unsigned anchor : 1;
};

typedef struct {
   int nr;
   int alloc;
   Arr(Entry) entries;
   Entry* first;
   Entry* last;
   
   // were common records found?
   Ulong has_matches;
   XdfEnv *env;
   XpParam const *xpp;
} DiffMap;

private int 
is_anchor(XpParam const *xpp, CS line) {
   for (int i = 0; i < (int)xpp->anchors_nr; i++) {
      if (!STRNCMP(line, xpp->anchors[i], STRLEN(xpp->anchors[i])))
         return 1;
   }
   return 0;
}

// The argument "pass" is 1 for the first file, 2 for the second.
private void insert_record(XpParam const *xpp, int line, DiffMap* map, int pass) {
   Arr(Record*) records = pass == 1 ? map->env->xdf1.recs : map->env->xdf2.recs;
   Record *record = records[line - 1];
   //After xdl_prepare_env() (or more precisely, due to xdl_classify_record()), the "ha" member of
   //the records (AKA lines) is _not_ the hash anymore, but a linearized version of it. In other 
   //words, the "ha" member is guaranteed to start with 0 and the second record's ha can only be 
   //0 or 1, etc.
   //
   //So we multiply ha by 2 in the hope that the hashing was "unique enough".
   int index = (int)((record->ha << 1) % map->alloc);

   while (map->entries[index].line1) {
      if (map->entries[index].hash != record->ha) {
         if (++index >= map->alloc)
            index = 0;
         continue;
      }
      if (pass == 2)
         map->has_matches = 1;
      if (pass == 1 || map->entries[index].line2)
         map->entries[index].line2 = NON_UNIQUE;
      else
         map->entries[index].line2 = line;
      return;
   }
   if (pass == 2)
      return;
   map->entries[index].line1 = line;
   map->entries[index].hash = record->ha;
   map->entries[index].anchor = is_anchor(xpp, map->env->xdf1.recs[line - 1]->ptr);
   if (!map->first)
      map->first = map->entries + index;
   if (map->last) {
      map->last->next = map->entries + index;
      map->entries[index].previous = map->last;
   }
   map->last = map->entries + index;
   map->nr++;
}

//This function has to be called for each recursion into the inter-hunk parts, as previously 
//non-unique lines can become unique when being restricted to a smaller part of the files.
//
//It is assumed that env has been prepared using xdl_prepare().
private int 
fill_hashmap(
      XpParam const *xpp, 
      XdfEnv *env,
      DiffMap* diffResult,
      int line1, 
      int count1, 
      int line2, 
      int count2
) {
   diffResult->xpp = xpp;
   diffResult->env = env;

   // We know exactly how large we want the hash map
   diffResult->alloc = count1 * 2;
   if (!XDL_CALLOC_ARRAY(diffResult->entries, diffResult->alloc))
      return -1;

   // First, fill with entries from the first file
   while (count1--)
      insert_record(xpp, line1++, diffResult, 1);

   // Then search for matches in the second file
   while (count2--)
      insert_record(xpp, line2++, diffResult, 2);

   return 0;
}

//Find the longest sequence with a smaller last element (meaning a smaller
//line2, as we construct the sequence with entries ordered by line1).
private int binary_search(Entry **sequence, int longest, Entry *entry) {
   int left = -1, right = longest;

   while (left + 1 < right) {
      int middle = left + (right - left) / 2;
      // by construction, no two entries can be equal
      if (sequence[middle]->line2 > entry->line2)
         right = middle;
      else
         left = middle;
   }
   // return the index in "sequence", _not_ the sequence length
   return left;
}

//The idea is to start with the list of common unique lines sorted by
//the order in file1.  For each of these pairs, the longest (partial)
//sequence whose last element's line2 is smaller is determined.
//
//For efficiency, the sequences are kept in a list containing exactly one
//item per sequence length: the sequence with the smallest last
//element (in terms of line2).
private int find_longest_common_sequence(DiffMap* map, Entry **res) {
   Entry **sequence;
   int longest = 0, i;
   Entry *entry;

   //If not -1, this entry in sequence must never be overridden.
   //Therefore, overriding entries before this has no effect, so
   //do not do that either.
   int anchorInd = -1;

   if (!XDL_ALLOC_ARRAY(sequence, map->nr))
      return -1;

   for (entry = map->first; entry; entry = entry->next) {
      if (!entry->line2 || entry->line2 == NON_UNIQUE)
         continue;
      i = binary_search(sequence, longest, entry);
      entry->previous = i < 0 ? NULL : sequence[i];
      ++i;
      if (i <= anchorInd)
         continue;
      sequence[i] = entry;
      if (entry->anchor) {
         anchorInd = i;
         longest = anchorInd + 1;
      } ei (i == longest) {
         longest++;
      }
   }

   // No common unique lines were found */
   if (!longest) {
      *res = NULL;
      eeglFree(sequence);
      return 0;
   }

   // Iterate starting at the last element, adjusting the "next" members
   entry = sequence[longest - 1];
   entry->next = NULL;
   while (entry->previous) {
      entry->previous->next = entry;
      entry = entry->previous;
   }
   *res = entry;
   eeglFree(sequence);
   return 0;
}

private int 
match(DiffMap* map, int line1, int line2) {
   Record *record1 = map->env->xdf1.recs[line1 - 1];
   Record *record2 = map->env->xdf2.recs[line2 - 1];
   return record1->ha == record2->ha;
}

private int patience_diff(XpParam const *xpp, XdfEnv *env,
      int line1, int count1, int line2, int count2);

private int 
walk_common_sequence(
   DiffMap* map, Entry *first, int line1, int count1, int line2, int count2
) {
   int end1 = line1 + count1, end2 = line2 + count2;
   int next1, next2;

   for (;;) {
      // Try to grow the line ranges of common lines
      if (first) {
         next1 = first->line1;
         next2 = first->line2;
         while (next1 > line1 && next2 > line2 && match(map, next1 - 1, next2 - 1)) {
            next1--;
            next2--;
         }
      } else {
         next1 = end1;
         next2 = end2;
      }
      while (line1 < next1 && line2 < next2 && match(map, line1, line2)) {
         line1++;
         line2++;
      }

      // Recurse 
      if ((next1 > line1 || next2 > line2)
         && (patience_diff(map->xpp, map->env, line1, next1 - line1, line2, next2 - line2))
      ) 
         return -1;

      if (!first)
         return 0;

      while (first->next &&
            first->next->line1 == first->line1 + 1 &&
            first->next->line2 == first->line2 + 1)
         first = first->next;

      line1 = first->line1 + 1;
      line2 = first->line2 + 1;

      first = first->next;
   }
}


private int
fall_back_to_classic_diff(DiffMap* map, int line1, int count1, int line2, int count2) {
   XpParam xpp;

   memset(&xpp, 0, sizeof(xpp));
   xpp.flags = map->xpp->flags & ~XDF_DIFF_ALGORITHM_MASK;

   return xdl_fall_back_diff(map->env, &xpp, line1, count1, line2, count2);
}

//Recursively find the longest common sequence of unique lines,
//and if none was found, ask xdl_do_diff() to do the job.
//
//This function assumes that env was prepared with xdl_prepare_env().
private int 
patience_diff( XpParam const *xpp, XdfEnv *env, int line1, int count1, int line2, int count2) {
   DiffMap map;
   Entry* first;
   int diffResult = 0;

   // trivial case: one side is empty
   if (!count1) {
      while(count2--)
         env->xdf2.rchg[line2++ - 1] = 1;
      return 0;
   } ei (!count2) {
      while(count1--)
         env->xdf1.rchg[line1++ - 1] = 1;
      return 0;
   }

   memset(&map, 0, sizeof(map));
   if (fill_hashmap(xpp, env, &map, line1, count1, line2, count2))
      return -1;

   // are there any matching lines at all?
   if (!map.has_matches) {
      while(count1--)
         env->xdf1.rchg[line1++ - 1] = 1;
      while(count2--)
         env->xdf2.rchg[line2++ - 1] = 1;
      eeglFree(map.entries);
      return 0;
   }

   diffResult = find_longest_common_sequence(&map, &first);
   if (diffResult)
      goto out;
   if (first)
      diffResult = walk_common_sequence(&map, first, line1, count1, line2, count2);
   else
      diffResult = fall_back_to_classic_diff(&map, line1, count1, line2, count2);
 out:
   eeglFree(map.entries);
   return diffResult;
}

private int
xdl_do_patience_diff(XpParam const *xpp, XdfEnv *env) {
   return patience_diff(xpp, env, 1, env->xdf1.nrec, 1, env->xdf2.nrec);
}

//}}}


#define MAX_PTR   INT_MAX
#define MAX_CNT   INT_MAX

#define LINE_END(n) (line##n + count##n - 1)
#define LINE_END_PTR(n) (*line##n + *count##n - 1)


declStruct(XdRecord);
struct XdRecord {
   Unt ptr;
   Unt cnt;
   XdRecord* next;
};

typedef struct {
   XdRecord** records; // an occurrence
   XdRecord** line_map; // map of line to record chain
   ChaStore rcha;
   Unt* next_ptrs;
   Unt table_bits;
   Unt records_size;
   Unt line_map_size;

   Unt max_chain_length;
   Unt key_shift;
   Unt ptr_shift;

   Unt cnt;
   Unt has_common;

   XdfEnv* env;
   XpParam const* xpp;
} HistIndex;

typedef struct {
   Unt begin1;
   Unt end1;
   Unt begin2;
   Unt end2;
} DRegion;

#define LINE_MAP(i, a) (i->line_map[(a) - i->ptr_shift])

#define NEXT_PTR(index, ptr) \
   (index->next_ptrs[(ptr) - index->ptr_shift])

#define CNT(index, ptr) \
   ((LINE_MAP(index, ptr))->cnt)

#define REC(env, s, l) \
   (env->xdf##s.recs[l - 1])

private int 
cmp_recs(Record *r1, Record *r2) {
   return r1->ha == r2->ha;
}

#define CMP(i, s1, l1, s2, l2) \
   (cmp_recs(REC(i->env, s1, l1), REC(i->env, s2, l2)))

#define TABLE_HASH(index, side, line) \
   XDL_HASHLONG((REC(index->env, side, line))->ha, index->table_bits)

private int 
scanA(HistIndex* index, int line1, int count1) {
   int ptr, tbl_idx;
   Unt chain_len;
   XdRecord** rec_chain;
   XdRecord* rec;

   for (ptr = LINE_END(1); line1 <= ptr; ptr--) {
      tbl_idx = TABLE_HASH(index, 1, ptr);
      rec_chain = index->records + tbl_idx;
      rec = *rec_chain;

      chain_len = 0;
      while (rec) {
         if (CMP(index, 1, rec->ptr, 1, ptr)) {
            //ptr is identical to another element. Insert
            //it onto the front of the existing element chain.
            NEXT_PTR(index, ptr) = rec->ptr;
            rec->ptr = ptr;
            // cap rec->cnt at MAX_CNT
            rec->cnt = XDL_MIN(MAX_CNT, rec->cnt + 1);
            LINE_MAP(index, ptr) = rec;
            goto continue_scan;
         }

         rec = rec->next;
         chain_len++;
      }

      if (chain_len == index->max_chain_length)
         return -1;

      //This is the first time we have ever seen this particular
      //element in the sequence. Construct a new chain for it.
      if (!(rec = xdl_cha_alloc(&index->rcha)))
         return -1;
      rec->ptr = ptr;
      rec->cnt = 1;
      rec->next = *rec_chain;
      *rec_chain = rec;
      LINE_MAP(index, ptr) = rec;

continue_scan:
      ; // no op
   }

   return 0;
}

private int
try_lcs(HistIndex* index, DRegion* lcs, int b_ptr, int line1, int count1, int line2, int count2) {
   Unt b_next = b_ptr + 1;
   XdRecord* rec = index->records[TABLE_HASH(index, 2, b_ptr)];
   unsigned int as, ae, bs, be, np, rc;
   int should_break;

   for (; rec; rec = rec->next) {
      if (rec->cnt > index->cnt) {
         if (!index->has_common)
            index->has_common = CMP(index, 1, rec->ptr, 2, b_ptr);
         continue;
      }

      as = rec->ptr;
      if (!CMP(index, 1, as, 2, b_ptr))
         continue;

      index->has_common = 1;
      for (;;) {
         should_break = 0;
         np = NEXT_PTR(index, as);
         bs = b_ptr;
         ae = as;
         be = bs;
         rc = rec->cnt;

         while (line1 < (int)as && line2 < (int)bs
            && CMP(index, 1, as - 1, 2, bs - 1)) {
            as--;
            bs--;
            if (1 < rc)
               rc = XDL_MIN(rc, CNT(index, as));
         }
         while ((int)ae < LINE_END(1) && (int)be < LINE_END(2)
            && CMP(index, 1, ae + 1, 2, be + 1)) {
            ae++;
            be++;
            if (1 < rc)
               rc = XDL_MIN(rc, CNT(index, ae));
         }

         if (b_next <= be)
            b_next = be + 1;
         if (lcs->end1 - lcs->begin1 < ae - as || rc < index->cnt) {
            lcs->begin1 = as;
            lcs->begin2 = bs;
            lcs->end1 = ae;
            lcs->end2 = be;
            index->cnt = rc;
         }

         if (np == 0)
            break;

         while (np <= ae) {
            np = NEXT_PTR(index, np);
            if (np == 0) {
               should_break = 1;
               break;
            }
         }

         if (should_break)
            break;

         as = np;
      }
   }
   return b_next;
}

private int 
fall_back_to_classic_diff1(
   XpParam const* xpp, XdfEnv* env, int line1, int count1, int line2, int count2
) {
   XpParam xpparam;
   memset(&xpparam, 0, sizeof(xpparam));
   xpparam.flags = xpp->flags & ~XDF_DIFF_ALGORITHM_MASK;

   return xdl_fall_back_diff(env, &xpparam, line1, count1, line2, count2);
}

private inline void 
free_index(HistIndex* index) {
   eeglFree(index->records);
   eeglFree(index->line_map);
   eeglFree(index->next_ptrs);
   xdl_cha_free(&index->rcha);
}

private int 
find_lcs(
   XpParam const* xpp, XdfEnv* env, DRegion* lcs, int line1, int count1, int line2, int count2
) {
   int b_ptr;
   int ret = -1;
   HistIndex index;
   memset(&index, 0, sizeof(index));

   index.env = env;
   index.xpp = xpp;

   index.records = NULL;
   index.line_map = NULL;
   // in case of early xdl_cha_free()
   index.rcha.head = NULL;

   index.table_bits = xdl_hashbits(count1);
   index.records_size = 1 << index.table_bits;
   if (!XDL_CALLOC_ARRAY(index.records, index.records_size))
      goto cleanup;

   index.line_map_size = count1;
   if (!XDL_CALLOC_ARRAY(index.line_map, index.line_map_size))
      goto cleanup;

   if (!XDL_CALLOC_ARRAY(index.next_ptrs, index.line_map_size))
      goto cleanup;

   // lines / 4 + 1 comes from xprepare.c:xdl_prepare_ctx()
   if (xdl_cha_init(&index.rcha, sizeof(Record), count1 / 4 + 1) < 0)
      goto cleanup;

   index.ptr_shift = line1;
   index.max_chain_length = 64;

   if (scanA(&index, line1, count1))
      goto cleanup;

   index.cnt = index.max_chain_length + 1;

   for (b_ptr = line2; b_ptr <= LINE_END(2); )
      b_ptr = try_lcs(&index, lcs, b_ptr, line1, count1, line2, count2);

   if (index.has_common && index.max_chain_length < index.cnt)
      ret = 1;
   else
      ret = 0;

cleanup:
   free_index(&index);
   return ret;
}

private int 
histogram_diff(XpParam const* xpp, XdfEnv* env, int line1, int count1, int line2, int count2) {
   DRegion lcs;
   int lcs_found;
   int diffResult;
redo:
   diffResult = -1;

   if (count1 <= 0 && count2 <= 0)
      return 0;

   if (LINE_END(1) >= MAX_PTR)
      return -1;

   if (!count1) {
      while(count2--)
         env->xdf2.rchg[line2++ - 1] = 1;
      return 0;
   } ei (!count2) {
      while(count1--)
         env->xdf1.rchg[line1++ - 1] = 1;
      return 0;
   }

   memset(&lcs, 0, sizeof(lcs));
   lcs_found = find_lcs(xpp, env, &lcs, line1, count1, line2, count2);
   if (lcs_found < 0)
      goto out;
   ei (lcs_found)
      diffResult = fall_back_to_classic_diff1(xpp, env, line1, count1, line2, count2);
   else {
      if (lcs.begin1 == 0 && lcs.begin2 == 0) {
         while (count1--)
            env->xdf1.rchg[line1++ - 1] = 1;
         while (count2--)
            env->xdf2.rchg[line2++ - 1] = 1;
         diffResult = 0;
      } else {
         diffResult = histogram_diff(xpp, env,
                  line1, lcs.begin1 - line1,
                  line2, lcs.begin2 - line2);
         if (diffResult)
            goto out;
         //diffResult = histogram_diff(xpp, env,
         //           lcs.end1 + 1, LINE_END(1) - lcs.end1,
         //           lcs.end2 + 1, LINE_END(2) - lcs.end2);
         //but let's optimize tail recursion ourself:
         count1 = LINE_END(1) - lcs.end1;
         line1 = lcs.end1 + 1;
         count2 = LINE_END(2) - lcs.end2;
         line2 = lcs.end2 + 1;
         goto redo;
      }
   }
out:
   return diffResult;
}

private int
xdl_do_histogram_diff(XpParam const *xpp, XdfEnv *env) {
   return histogram_diff(xpp, env,
      env->xdf1.dstart + 1, env->xdf1.dend - env->xdf1.dstart + 1,
      env->xdf2.dstart + 1, env->xdf2.dend - env->xdf2.dstart + 1);
}

private long xdl_get_rec(XdFile *xdf, long ri, Byte** rec) {
   *rec = xdf->recs[ri]->ptr;
   return xdf->recs[ri]->size;
}

private int xdl_emit_record(XdFile *xdf, long ri, CS pre, XdEmitCb* ecb) {
   long psize = (long)STRLEN(pre);
   CS rec;
   long size = xdl_get_rec(xdf, ri, OUT &rec);
   if (xdl_emit_diffrec(rec, size, pre, psize, ecb) < 0) {
      return -1;
   }

   return 0;
}

//Starting at the passed change atom, find the latest change atom to be included
//inside the differential hunk according to the specified configuration.
//Also advance xscr if the first changes must be discarded.
private XdChange *
xdl_get_hunk(XdChange** xscr, XdEmitConf const* xecfg) {
   XdChange *xch, *xchp, *lxch;
   long max_common = 2 * xecfg->ctxlen + xecfg->interhunkctxlen;
   long max_ignorable = xecfg->ctxlen;
   unsigned long ignored = 0; /* number of ignored blank lines */

   // remove ignorable changes that are too far before other changes */
   for (xchp = *xscr; xchp && xchp->ignore; xchp = xchp->next) {
      xch = xchp->next;

      if (xch == NULL || xch->i1 - (xchp->i1 + xchp->chg1) >= max_ignorable)
         *xscr = xch;
   }

   if (!*xscr)
      return NULL;

   lxch = *xscr;

   for (xchp = *xscr, xch = xchp->next; xch; xchp = xch, xch = xch->next) {
      long distance = xch->i1 - (xchp->i1 + xchp->chg1);
      if (distance > max_common)
         break;

      if (distance < max_ignorable && (!xch->ignore || lxch == xchp)) {
         lxch = xch;
         ignored = 0;
      } ei (distance < max_ignorable && xch->ignore) {
         ignored += xch->chg2;
      } ei (lxch != xchp &&
            xch->i1 + (long)ignored - (lxch->i1 + lxch->chg1) > max_common) {
         break;
      } ei (!xch->ignore) {
         lxch = xch;
         ignored = 0;
      } else {
         ignored += xch->chg2;
      }
   }

   return lxch;
}

typedef struct {
   long len;
   Byte buf[80];
} FuncLine;

private int
xdl_emit_diff(XdfEnv* xe, XdChange* xscr, XdEmitCb* ecb, XdEmitConf const* xecfg) {
   long s1, s2, e1, e2, lctx;
   XdChange *xch, *xche;
   FuncLine func_line;

   func_line.len = 0;

   for (xch = xscr; xch; xch = xche->next) {
      xche = xdl_get_hunk(&xch, xecfg);
      if (!xch)
         break;

      s1 = XDL_MAX(xch->i1 - xecfg->ctxlen, 0);
      s2 = XDL_MAX(xch->i2 - xecfg->ctxlen, 0);

      lctx = xecfg->ctxlen;
      lctx = XDL_MIN(lctx, xe->xdf1.nrec - (xche->i1 + xche->chg1));
      lctx = XDL_MIN(lctx, xe->xdf2.nrec - (xche->i2 + xche->chg2));

      e1 = xche->i1 + xche->chg1 + lctx;
      e2 = xche->i2 + xche->chg2 + lctx;

      //Emit current hunk header.

      if (!(xecfg->flags & XDL_EMIT_NO_HUNK_HDR) 
            && xdl_emit_hunk_hdr(s1 + 1, e1 - s1, s2 + 1, e2 - s2,
                  func_line.buf, func_line.len, ecb) < 0
      )
         return -1;

      //Emit pre-context.
      for (; s2 < xch->i2; s2++)
         if (xdl_emit_record(&xe->xdf2, s2, S" ", ecb) < 0)
            return -1;

      for (s1 = xch->i1, s2 = xch->i2;; xch = xch->next) {
         //Merge previous with current change atom.
         for (; s1 < xch->i1 && s2 < xch->i2; s1++, s2++)
            if (xdl_emit_record(&xe->xdf2, s2, S" ", ecb) < 0)
               return -1;

         //Removes lines from the first file.
         for (s1 = xch->i1; s1 < xch->i1 + xch->chg1; s1++)
            if (xdl_emit_record(&xe->xdf1, s1, S"-", ecb) < 0)
               return -1;

         //Adds lines from the second file.
         for (s2 = xch->i2; s2 < xch->i2 + xch->chg2; s2++) {
            if (xdl_emit_record(&xe->xdf2, s2, S"+", ecb) < 0)
               return -1;
         } 

         if (xch == xche)
            break;
         s1 = xch->i1 + xch->chg1;
         s2 = xch->i2 + xch->chg2;
      }

      //Emit post-context.
      for (s2 = xche->i2 + xche->chg2; s2 < e2; s2++) {
         if (xdl_emit_record(&xe->xdf2, s2, S" ", ecb) < 0)
            return -1;
      } 
   }

   return 0;
}

//}}}
