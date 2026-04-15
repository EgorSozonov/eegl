//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov
 
//## memory.c: low-level functions for managing memory, including the text

#include "eegl.h"

#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/stat.h> // for stat, fstat

//{{{allocations
//{{{ Arena

#define CHUNK_QUANT 32768

typedef struct ArenaChunk ArenaChunk;

struct ArenaChunk { // :ArenaChunk
   Unt size;
   ArenaChunk* next;
   char memory[]; // flexible array member
};

struct Arena { // :Arena
   ArenaChunk* firstChunk;
   ArenaChunk* currChunk;
   int currInd;
};

Arena*
createArena() { //:createArena
   Arena* result = malloc(sizeof(Arena));

   Unt firstChunkSize = (CHUNK_QUANT - 32);
   ArenaChunk* firstChunk = malloc(firstChunkSize);
   if (!result || !firstChunk)
      { abort(); }

   firstChunk->size = firstChunkSize - sizeof(ArenaChunk);
   firstChunk->next = null;
   result->firstChunk = firstChunk;
   result->currChunk = firstChunk;
   result->currInd = 0;
   return result;
}

private Unt
calculateChunkSize(Unt allocSize) { //:calculateChunkSize
// Calculates memory for a new chunk. Memory is quantized and is always 32 bytes less
// 32 for any possible padding malloc might use internally,
// so that the total allocation size is a good even number of OS memory pages
   Unt fullMemory = sizeof(ArenaChunk) + allocSize + 32;
   // struct header + main memory chunk + space for malloc bookkeep

   int mallocMemory = fullMemory < CHUNK_QUANT
                  ? CHUNK_QUANT
                  : (fullMemory % CHUNK_QUANT > 0
                     ? (fullMemory/CHUNK_QUANT + 1)*CHUNK_QUANT
                     : fullMemory);

   return mallocMemory - 32;
}

void*
allocateOnArena(Unt allocSize, Arena* a) { //:allocateOnArena
// Allocate memory in the arena, malloc'ing a new chunk if needed
   if ((Unt)a->currInd + allocSize >= a->currChunk->size) {
      if (a->currChunk->next != null && a->currChunk->next->size < allocSize) {
         // the next chunk is big enough, so we skip the rest of this chunk and move on
         lo("reusing cleared memory from the arena!");
         a->currChunk = a->currChunk->next;
         a->currInd = 0;
      } else { // we need to allocate new chunk

         Unt newSize = calculateChunkSize(allocSize);
         ArenaChunk* newChunk = malloc(newSize);
         if (!newChunk) {
            perror("malloc error when allocating arena chunk");
            exit(EXIT_FAILURE);
         };
         // sizeof counts everything but the flexible array member, that's why we subtract it
         newChunk->size = newSize - sizeof(ArenaChunk);
         newChunk->next = a->currChunk->next; // if the arena has a (small) tail, don't lose it

         a->currChunk->next = newChunk;
         a->currChunk = newChunk;
         a->currInd = 0;
      }

   }
   void* result = (void*)(a->currChunk->memory + (a->currInd));
   a->currInd += allocSize;
   if (allocSize % 4 != 0)  {
      a->currInd += (4 - (allocSize % 4));
   }
   return result;
}

void
deleteArena(Arena* ar) { //:deleteArena
// Returns memory of the arena to the OS
   ArenaChunk* curr = ar->firstChunk;
   while (curr != null) {
      ArenaChunk* nextToFree = curr->next;
      free(curr);
      curr = nextToFree;
   }
   free(ar);
}

//private void
//clearArena(Arena* a) { //:clearArena
//// Clears the memory of the arena for reuse. Does not free memory.
//   a->currChunk = a->firstChunk;
//   a->currInd = 0;
//}

void
arenaTryFree(void* start, Unt len, Arena* a) {
// If this memory span is at the very end of this arena, then free it by rewinding
   if ((void*)&(a->currChunk->memory) + (a->currInd - len) == start) {
      a->currInd -= len;
   }
}

//}}}

#if defined(MEM_PROFILE) || defined(PROTO)

# define MEM_SIZES  8200
private Ulong mem_allocs[MEM_SIZES];
private Ulong mem_frees[MEM_SIZES];
private Ulong mem_allocated;
private Ulong mem_freed;
private Ulong mem_peak;
private Ulong num_alloc;
private Ulong num_freed;

private void
mem_pre_alloc_s(Unt *sizep) {
    *sizep += sizeof(Unt);
}

private void
mem_pre_alloc_l(Unt *sizep) {
   *sizep += sizeof(Unt);
}

private void
mem_post_alloc(void **pp, Unt size) {
   if (*pp == NULL)
      return;
   size -= sizeof(Unt);
   *(Ulong *)*pp = size;
   if (size <= MEM_SIZES-1)
      mem_allocs[size-1]++;
   else
      mem_allocs[MEM_SIZES-1]++;
   mem_allocated += size;
   if (mem_allocated - mem_freed > mem_peak)
      mem_peak = mem_allocated - mem_freed;
   num_alloc++;
   *pp = (void *)((char *)*pp + sizeof(Unt));
}

private void
mem_pre_free(void **pp) {
   Ulong size;

   *pp = (void *)((char *)*pp - sizeof(Unt));
   size = *(Unt *)*pp;
   if (size <= MEM_SIZES-1)
      mem_frees[size-1]++;
   else
      mem_frees[MEM_SIZES-1]++;
   mem_freed += size;
   num_freed++;
}

// called on exit via atexit()
void
eeMemProfileDump(void) {
   int i, j;

   printf("\r\n");
   j = 0;
   for (i = 0; i < MEM_SIZES - 1; i++) {
      if (mem_allocs[i] == 0 && mem_frees[i] == 0)
         continue;

      if (mem_frees[i] > mem_allocs[i])
         printf("\r\n%s", _("ERROR: "));
      printf("[%4d / %4lu-%-4lu] ", i + 1, mem_allocs[i], mem_frees[i]);
      j++;
      if (j > 3) {
         j = 0;
         printf("\r\n");
      }
   }

    i = MEM_SIZES - 1;
   if (mem_allocs[i]) {
   printf("\r\n");
   if (mem_frees[i] > mem_allocs[i])
       puts(_("ERROR: "));
   printf("[>%d / %4lu-%-4lu]", i, mem_allocs[i], mem_frees[i]);
    }

    printf(_("\n[bytes] total alloc-freed %lu-%lu, in use %lu, peak use %lu\n"),
       mem_allocated, mem_freed, mem_allocated - mem_freed, mem_peak);
    printf(_("[calls] total re/malloc()'s %lu, total free()'s %lu\n\n"),
       num_alloc, num_freed);
}

#endif // MEM_PROFILE

int
alloc_does_fail(Unt size) {
   if (alloc_fail_countdown == 0) {
      if (--alloc_fail_repeat <= 0)
          alloc_fail_id = 0;
      do_outofmem_msg(size);
      return TRUE;
   }
   --alloc_fail_countdown;
   return FALSE;
}

// Some memory is reserved for error messages and for being able to
// call mf_release_all(), which needs some memory for mf_trans_add().
#define KEEP_ROOM (2 * 8192L)
#define KEEP_ROOM_KB (KEEP_ROOM / 1024L)

//The normal way to allocate memory. Handles an out-of-memory situation
//as well as possible, exit the process with cleanup when that doesn't help.
//This means the return value doesn't need to be checked for null.
void *
alloc(Unt size) {
   return lalloc(size, TRUE);
}

//Try to make a big allocation. Quietly return null if unsucessful.
void*
tryBigAlloc(Unt size) {
   static int releasing = FALSE;  // don't do mf_release_all() recursive

#ifdef MEM_PROFILE
   //Safety check for allocating zero bytes
   if (size == 0) {
      //Don't hide this message
      emsg_silent = 0;
      internalErrMsg(e_internal_error_lalloc_zero);
      return NULL;
   }

   mem_pre_alloc_l(&size);
#endif

   //Loop when out of memory: Try to release some memfile blocks and
   //if some blocks are released call malloc again.
   void   *p;          // pointer to new storage space
   for (;;) {
      if ((p = malloc(size)) != NULL) {
         // 1. No check for available memory: Just return.
         goto success;
      }
      //Remember that mf_release_all() is being called to avoid an endless
      //loop, because mf_release_all() may call alloc() recursively.
      if (releasing)
         break;
      releasing = TRUE;

      clear_sb_text(TRUE);         // free any scrollback text
      int try_again = mf_release_all(); // release as many blocks as possible

      releasing = FALSE;
      if (!try_again)
         break;
   }

success:
#ifdef MEM_PROFILE
   mem_post_alloc(&p, size);
#endif
   return p;
}

// alloc() with an ID for alloc_fail().
void *
alloc_id(Unt size, AllocId id UNUSED) {
   if (alloc_fail_id == id && alloc_does_fail(size))
      return NULL;
   return lalloc(size, TRUE);
}

// Allocate memory and set all bytes to zero.
void *
allocZeroed(Unt size) {
   void* p = lalloc(size, TRUE);
   (void)memset(p, 0, size);
   return p;
}

// Same as allocZeroed() but with allocation id for testing
void *
allocZeroed_id(Unt size, AllocId id UNUSED) {
   if (alloc_fail_id == id && alloc_does_fail(size))
      return NULL;
   return allocZeroed(size);
}

// Allocate memory like lalloc() and set all bytes to zero.
void *
lallocZeroed(Unt size, int message){
   void* p = lalloc(size, message);
   if (p)
      (void)memset(p, 0, size);
   return p;
}

// Low-level memory allocation function. This is used often, KEEP IT FAST!
void *
lalloc(Unt size, int message) {
   static int   releasing = FALSE;  // don't do mf_release_all() recursive

#ifdef MEM_PROFILE
   // Safety check for allocating zero bytes
   if (size == 0) {
      // Don't hide this message
      emsg_silent = 0;
      internalErrMsg(e_internal_error_lalloc_zero);
      return NULL;
   }

   mem_pre_alloc_l(&size);
#endif

   //Loop when out of memory: Try to release some memfile blocks and
   //if some blocks are released call malloc again.
   void   *p;          // pointer to new storage space
   for (;;) {
      if ((p = malloc(size)) != NULL) {
         // 1. No check for available memory: Just return.
         goto success;
      }
      //Remember that mf_release_all() is being called to avoid an endless
      //loop, because mf_release_all() may call alloc() recursively.
      if (releasing)
          break;
      releasing = TRUE;

      clear_sb_text(TRUE);         // free any scrollback text
      int try_again = mf_release_all(); // release as many blocks as possible

      releasing = FALSE;
      if (!try_again)
         break;
   }

   if (message && p)
      do_outofmem_msg(size);
   mch_exit(2);
   
success:
#ifdef MEM_PROFILE
   mem_post_alloc(&p, size);
#endif
   return p;
}

// lalloc() with an ID for alloc_fail().
void *
lalloc_id(Unt size, int message, AllocId id UNUSED) {
   if (alloc_fail_id == id && alloc_does_fail(size))
      return NULL;
   return (lalloc(size, message));
}

#if defined(MEM_PROFILE) || defined(PROTO)
// realloc() with memory profiling.
void *
mem_realloc(void *ptr, Unt size) {
   void *p;

   mem_pre_free(&ptr);
   mem_pre_alloc_s(&size);

   p = realloc(ptr, size);

   mem_post_alloc(&p, size);

   return p;
}
#endif

//Avoid repeating the error message many times (they take 1 second each).
//Did_outofmem_msg is reset when a character is read.
void
do_outofmem_msg(Unt size) {
   if (did_outofmem_msg)
      return;

   //Don't hide this message
   emsg_silent = 0;

   //Must come first to avoid coming back here when printing the error
   //message fails, e.g. when setting v:errmsg.
   did_outofmem_msg = TRUE;

   showErrFmtMsg(_(e_out_of_memory_allocating_nr_bytes), (Ulong)size);

   if (starting == NO_SCREEN)
      //Not even finished with initializations and already out of
      //memory?  Then nothing is going to work, exit.
      mch_exit(123);
}


#if defined(EXITFREE) || defined(PROTO)

// Free everything that we allocated. Can be used to detect memory leaks, e.g., with ccmalloc.
// NOTE: This is tricky!  Things are freed that functions depend on.  Don't be
// surprised if Eegl crashes...
// Some things can't be freed, esp. things local to a library function.
void
free_all_mem(void) {
   //When we cause a crash here it is caught and Eegl tries to exit cleanly.
   //Don't try freeing everything again.
   if (entered_free_all_mem)
      return;
      
   entered_free_all_mem = TRUE;
   // Don't want to trigger autocommands from here on.
   block_autocmds();

   // Close all tabs and portals. Reset 'equalalways' to avoid redraws.
   p_ea = FALSE;
   if (firstTabG != NULL && firstTabG->next != NULL)
      executeCommLine(S"tabonly!");
   if (!ONLY_ONE_PORTAL)
      executeCommLine(S"only!");

   // Free all spell info.
   spell_free_all();

   ui_remove_balloon();
   if (curPor)
      close_all_popups(TRUE);

   // Clear user commands (before deleting books).
   ex_comclear(NULL);

   // When exiting from mainerr_arg_missing curBook has not been initialized, and not much else
   if (curBook) {
      // Clear menus.
      executeCommLine(S"aunmenu *");
      executeCommLine(S"tlunmenu *");
      executeCommLine(S"menutranslate clear");
      // Clear mappings, abbreviations, breakpoints.
      executeCommLine(S"lmapclear");
      executeCommLine(S"xmapclear");
      executeCommLine(S"mapclear");
      executeCommLine(S"mapclear!");
      executeCommLine(S"abclear");
      executeCommLine(S"breakdel *");
   }

   free_findfile();

   // Obviously named calls.
   free_all_autocmds();
   clear_termcodes();
   free_all_marks();
   alist_clear(&argListG);
   free_homedir();
   free_users();
   free_search_patterns();
   free_old_sub();
   free_last_insert();
   free_insexpand_stuff();
   free_prev_shellcmd();
   free_regexp_stuff();
   free_tag_stuff();
   free_xim_stuff();
   free_cd_dir();
   free_signs();
   set_expr_line(NULL, NULL);
   if (curtab)
      diff_clear(curtab);
   clear_sb_text(TRUE);         // free any scrollback text

   // Free some global vars.
   eeglFree(username);
   eeglFree(lastCommlineG);
   eeglFree(newLastCommlineG);
   set_keep_msg(NULL, 0);

   // Clear commline history.
   p_hi = 0;
   init_history();
   clear_global_prop_types();

   free_quickfix();

   // Close all script inputs.
   close_all_scripts();

   if (curPor)
      //Destroy all portals. Must come before freeing books.
      portFreeAll();

   //Free all option values. Must come after closing portals.
   optFreeAllOptions();

   //Free all books. Reset 'autochdir' to avoid accessing things that were freed already.
   Book* book;
   for (book = firstBook; book; ) {
      BookRef    bufref;
      bookStoreInRef(OUT &bufref, book);
      Book* nextBook = book->next;
      closeBook(NULL, book, DOBUF_WIPE, FALSE, FALSE);
      if (bookRefValid(&bufref))
         book = nextBook;   // didn't work, try next one
      else
         book = firstBook;
   }

   // Clear registers.
   clear_registers();
   ResetRedobuff();
   ResetRedobuff();

# if defined(FEAT_X11)
   eeglFree(serverDelayedStartName);
# endif

   // hilite info
   freeHilites();

   reset_last_sourcing();

   if (firstTabG) {
      freeTab(firstTabG);
      firstTabG = NULL;
   }

   // Machine-specific free.
   mch_free_mem();

   // message history
   for (;;) {
      if (delete_first_msg() == FAIL)
          break;
   } 

   channel_free_all();
   timer_free_all();
   // must be after channel_free_all() with unrefs partials
   eval_clear();
   // must be after eval_clear() with unrefs jobs
   job_free_all();

   free_termoptions();
   free_cur_term();

   // screenlines (can't display anything now!)
   free_screenlines();

   clear_hl_tables();

   eeglFree(IObuff);
   eeglFree(NameBuff);
   check_quickfix_busy();
   free_resub_eval_result();
   free_vbuf();
}
#endif

// Copy "p[len]" into allocated memory, ignoring ZERO characters.
CS
eeMemsave(Byte *p, Unt len) {
   Byte *ret = alloc(len);
   mch_memmove(ret, p, len);
   return ret;
}

// Replacement for free() that ignores NULL pointers. Also skip free() when exiting for sure, this
// helps when we caught a deadly signal that was caused by a crash in free().
// If you want to set NULL after calling this function, you should use EE_CLEAR() instead.
void
eeglFree(void* x) {
   if (x && !really_exiting) {
#ifdef MEM_PROFILE
      mem_pre_free(&x);
#endif
      free(x);
   }
}

void
eeglFreeString(CS x) {
   if (x && *x != ZERO && !really_exiting) {
#ifdef MEM_PROFILE
      mem_pre_free(&x);
#endif
      free(x);
   }
}

//Return total amount of memory available in Kbyte. Doesn't change when memory has been allocated.
Ulong
mch_total_mem(int special UNUSED) {
   Ulong   mem = 0;
   Ulong   shiftright = 10;  // how much to shift "mem" right for Kbyte

   if (mem == 0) {
      struct sysinfo sinfo;

      // Linux way of getting amount of RAM available
      if (sysinfo(&sinfo) == 0) {
         // avoid overflow as much as possible
         while (shiftright > 0 && (sinfo.mem_unit & 1) == 0) {
            sinfo.mem_unit = sinfo.mem_unit >> 1;
            --shiftright;
         }
         mem = sinfo.totalram * sinfo.mem_unit;
      }
   }

   // Return the minimum of the physical memory and the user limit, because
   // using more than the user limit may cause Eegl to be terminated.
   {
   struct rlimit   rlp;

   if (getrlimit(RLIMIT_DATA, &rlp) == 0
      && rlp.rlim_cur < ((rlim_t)1 << (sizeof(Ulong) * 8 - 1))
      && rlp.rlim_cur != RLIM_INFINITY
      && ((Ulong)rlp.rlim_cur >> 10) < (mem >> shiftright)
      )
   {
       mem = (Ulong)rlp.rlim_cur;
       shiftright = 10;
   }
   }

   if (mem > 0)
      return mem >> shiftright;
   return (Ulong)0x1fffff;
}

#if defined(EXITFREE) || defined(PROTO)

void
mch_free_mem(void){
# if defined(FEAT_X11)
   if (clipboard.owned)
      clip_lose_selection(&clipboard);
# endif
# if defined(FEAT_X11)
   if (xterm_Shell != (Widget)0)
      XtDestroyWidget(xterm_Shell);
#  ifndef LESSTIF_VERSION
   // Lesstif crashes here, lose some memory
   if (xterm_dpy)
      XtCloseDisplay(xterm_dpy);
   if (app_context != (XtAppContext)NULL) {
      XtDestroyApplicationContext(app_context);
#   ifdef FEAT_X11
      x11DisplayG = NULL; // freed by XtDestroyApplicationContext()
#   endif
   }
#  endif
# endif
# if defined(FEAT_X11)
   if (x11DisplayG != NULL && x11DisplayG != xterm_dpy)
   XCloseDisplay(x11DisplayG);
# endif
    EE_CLEAR(signal_stack);
}
#endif

//}}}
//{{{resource cleanup at exit

// Output a newline when exiting. Make sure the newline goes to the same stream as the text.
private void
exit_scroll(void) {
   if (silentModeG)
      return;
   if (newline_on_exit || msg_didout) {
      if (msg_use_printf()) {
         if (info_message)
            mch_msg("\n");
         else
            mch_errmsg("\r\n");
      } else
         out_char('\n');
   } ei (!is_not_a_term()) {
      restore_cterm_colors();      // get original colors back
      msg_clr_eos_force();      // clear the rest of the display
      windgoto((int)visibleRowsG - 1, 0);   // may have moved the cursor
   }
}


//Low-level resoure cleanup function
void
mch_exit(int r) {
   exiting = TRUE;
   {
   termSetMode(TMODE_COOK);

   //When t_ti is not empty but it doesn't cause swapping terminal pages, need to output a 
   //newline when msg_didout is set. But when t_ti does swap pages it should not go to the shell 
   //page. Do this before termStopTerminfo().
   if (swapping_screen() && !newline_on_exit)
      exit_scroll();

   // Stop termcap: May need to check for KS_CRV response, which requires RAW mode.
   termStopTerminfo();

   //A newline is only required after a message in the alternate screen.
   //This is set to TRUE by wait_return().
   if (!swapping_screen() || newline_on_exit)
      exit_scroll();

   //Cursor may have been switched off without calling starttermcap()
   //when doing "eegl -u vimrc" and vimrc contains ":q".
   if (fullScreenG)
      cursor_on();
   }
   out_flush();
   ml_close_all(TRUE);      // remove all memfiles

#ifdef USE_GCOV_FLUSH
    // Flush coverage info before possibly being killed by a deadly signal.
    __gcov_flush();
#endif

   may_core_dump();


#ifdef EXITFREE
   free_all_mem();
#endif

   exit(r);
}

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
static int   dontReleaseBlocksS = FALSE;

typedef struct Block0      Block0;      // contents of the first block
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

#define B0_FNAME_SIZE_ORG   900   // what it was in older versions
#define B0_FNAME_SIZE_NOCRYPT   898   // 2 bytes used for other things
#define B0_FNAME_SIZE_CRYPT   890   // 10 bytes used for other things
#define B0_UNAME_SIZE      40
#define B0_HNAME_SIZE      40
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

//Block 0 holds all info about the swap file.
//
//NOTE: DEFINITION OF BLOCK 0 SHOULD NOT CHANGE! It would make all existing swap files unusable!
//
//If size of block0 changes anyway, adjust MIN_SWAP_PAGE_SIZE in eegl.h!!
//
//This block is built up of single bytes, to make it portable across
//different machines. b0_magic_* is used to check the byte order and size of
//variables, because the rest of the swap file is not portable.
struct Block0 {
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
   long   b0_magic_long;   // check for byte order of long
   int    b0_magic_int;   // check for byte order of int
   short  b0_magic_short;   // check for byte order of short
   Byte   b0_magic_char;   // check for last char
};

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

// argument for updateBlock0()
typedef enum {
   UB_FNAME = 0, // update timestamp and filename
   UB_SAME_DIR,  // update the B0_SAME_DIR flag
   UB_CRYPT      // update crypt key
} upd_block0_T;

private void updateBlock0(Book *book, upd_block0_T what);
private void set_b0_fname(Block0 *, Book *book);
private void set_b0_dir_flag(Block0 *b0p, Book *book);
private Tyme swapfile_info(CS);
private int recoverFileNames(Byte **, Byte *, int prepend_dot);
private Byte *findSwapName(Book *, Byte **, Byte *);
private void flushLine(Book *);
private BlockHeader* newDataBlock(MemFile *, int, int);
private BlockHeader *ml_new_ptr(MemFile *);
private BlockHeader *ml_find_line(Book *, LineNr, int);
private int ml_add_stack(Book *);
private void fixBlockStack(Book *, int);
private int b0_magic_wrong(Block0 *);
private int compareFnameWithInode(Byte *, Byte *, long);
private void longToChar(long, Byte *);
private long charToLong(CS);
private void updateChunk(Book *book, long line, long len, int updtype);

//Open a new memline for "book". Return FAIL for failure, OK otherwise.
int
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
      book->o.swapFile = FALSE;

   //When 'updatecount' is non-zero swap file may be opened later.
   if (swapEnabledG && book->o.swapFile)
      book->maySwap = TRUE;
   else
      book->maySwap = FALSE;

   // Open the memfile.  No swap file is created yet.
   mfp = mf_open(NULL, 0);
   if (mfp == NULL)
      goto error;

   book->mem.mfile = mfp;
   mfp->book = book;
   book->mem.flags = ML_EMPTY;
   book->mem.lineCount = 1;

   // fill Block0 struct and write page 0
   if ((hdr = mf_new(mfp, FALSE, 1)) == NULL)
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
   mch_memmove(b0p->b0_version, "EEGL ", 4);
   STRNCPY(b0p->b0_version + 4, Version, 6);
   longToChar((long)mfp->pageSize, b0p->b0_page_size);

   if (book->kind != BOOK_SPELL) {
      b0p->b0_dirty = book->wasModified ? B0_DIRTY : 0;
      b0p->b0_flags = 0;
      set_b0_fname(b0p, book);
      (void)get_user_name(b0p->b0_uname, B0_UNAME_SIZE);
      b0p->b0_uname[B0_UNAME_SIZE - 1] = ZERO;
      mch_get_host_name(b0p->b0_hname, B0_HNAME_SIZE);
      b0p->b0_hname[B0_HNAME_SIZE - 1] = ZERO;
      longToChar(mch_get_pid(), b0p->b0_pid);
   }

   //Always sync block number 0 to disk, so we can check the file name in
   //the swap file in findSwapName(). Don't do this for a help files or a spell book though.
   //Only works when there's a swapfile, otherwise it's done when the file is created.
   mf_put(mfp, hdr, TRUE, FALSE);
   if (book->kind != BOOK_HELP && book->kind != BOOK_SPELL)
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
   mf_put(mfp, hdr, TRUE, FALSE);

   //Allocate first data block and create an empty line 1.
   if ((hdr = newDataBlock(mfp, FALSE, 1)) == NULL)
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
         mf_put(mfp, hdr, FALSE, FALSE);
      mf_close(mfp, TRUE);       // will also free(mfp->fName)
   }
   book->mem.mfile = NULL;
   return FAIL;
}

//Prepare encryption for "book" for the current key and method.
//ml_setname() is called when the file name of "book" has been changed. It may rename the swap file.
void
ml_setname(Book *book) {
   int      success = FALSE;
   CS fname;

   MemFile* mfp = book->mem.mfile;
   if (mfp->fd < 0)   {    // there is no swap file yet
      //When 'updatecount' is 0 and 'noswapfile' there is no swap file.
      //For help files we will make one now.
      if (swapEnabledG && (commModifierG.cmod_flags & CMOD_NOSWAPFILE) == 0)
          ml_open_file(book);       // create a swap file
      return;
   }

   // Try all directories in the 'directory' option.
   CS dirp = p_dir;
   for (;;) {
      if (*dirp == ZERO)       // tried all directories, fail
         break;
      fname = findSwapName(book, &dirp, mfp->fName); // alloc's fname
      if (dirp == NULL)       // out of memory
         break;
      if (fname == NULL)       // no file name found for this dir
         continue;

      // if the file name is the same we don't have to do anything
      if (fnamecmp(fname, mfp->fName) == 0) {
         eeglFree(fname);
         success = TRUE;
         break;
      }
      // need to close the swap file before renaming
      if (mfp->fd >= 0) {
         close(mfp->fd);
         mfp->fd = -1;
      }

      // try to rename the swap file
      if (eeRename(mfp->fName, fname) == 0) {
         success = TRUE;
         eeglFree(mfp->fName);
         mfp->fName = fname;
         eeglFree(mfp->fullFName);
         mf_set_ffname(mfp);
         updateBlock0(book, UB_SAME_DIR);
         break;
      }
      eeglFree(fname);       // this fname didn't work, try another
   }

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
void
ml_open_files(void) {
   Book   *book;
   FOR_ALL_BOOKS(book) {
      if (book->o.modifiable || book->wasModified)
         ml_open_file(book);
   } 
}

//Open a swap file for an existing memfile, if there is no swap file yet.
//If we are unable to find a file name, fName will be NULL
//and the memfile will be in memory only (no recovery possible).
void
ml_open_file(Book *book) {
   Arr(Byte) fname;

   MemFile* mfp = book->mem.mfile;
   if (!mfp || mfp->fd >= 0 || !book->o.swapFile || (commModifierG.cmod_flags & CMOD_NOSWAPFILE))
      return;      // nothing to do

   // For a spell book use a temp file name.
   if (book->kind == BOOK_SPELL) {
      fname = eeTempName('s', FALSE);
      if (fname)
         (void)mf_open_file(mfp, fname);   // consumes fname!
      book->maySwap = FALSE;
      return;
   }

   // Try all directories in @directory
   CS dirp = p_dir;
   for (;;) {
      if (*dirp == ZERO)
         break;
      //There is a small chance that between choosing the swap file name and creating it, another
      //Eegl creates the file. In that case the creation will fail and we will use another 
      //directory.
      fname = findSwapName(book, &dirp, NULL); // allocates fname
      if (dirp == NULL)
          break;  // out of memory
      if (fname == NULL)
          continue;
      if (mf_open_file(mfp, fname) == OK) {// consumes fname!
         // don't sync yet in ml_sync_all()
         mfp->mf_dirty = MF_DIRTY_YES_NOSYNC;
         updateBlock0(book, UB_SAME_DIR);

         // Flush block zero, so others can read it
         if (mf_sync(mfp, MFS_ZERO) == OK) {
            // Mark all blocks that should be in the swapfile as dirty. Needed for when the 
            // 'swapfile' option was reset, so that the swap file was deleted, and then on again.
            mf_set_dirty(mfp);
            break;
          }
          // Writing block 0 failed: close the file and try another dir
          mf_close_file(book, FALSE);
      }
   }

   if (*p_dir != ZERO && !mfp->fName) {
      need_wait_return = TRUE;   // call wait_return() later
      ++no_wait_return;
      (void)showErrFmtMsg(_(e_unable_to_open_swap_file_for_str_recovery_impossible),
             bookSpName(book) != NULL ? bookSpName(book) : book->currFileName
      );
      --no_wait_return;
   }

   // don't try to open a swap file again
   book->maySwap = FALSE;
}

//If still need to create a swap file, and starting to edit a not-readonly
//file, or reading into an existing book, create a swap file now.
void
check_need_swap(int newfile)  {    // reading file into new book
   int old_msg_silent = msg_silent; // might be reset by an E325 message

   if (curBook->maySwap && (curBook->o.modifiable || !newfile))
      ml_open_file(curBook);
   msg_silent = old_msg_silent;
}

// Close memline for book. If 'del_file' is TRUE, delete the swap file
void
ml_close(Book *book, int del_file) {
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
// When 'del_file' is TRUE, delete the memfiles.
// But don't delete files that were ":preserve"d when we are POSIX compatible.
void
ml_close_all(int del_file) {
   Book   *book;
   FOR_ALL_BOOKS(book) {
      ml_close(book, del_file);
   } 
   eeDelTempDir();      // delete created temp directory
}

// Close all memfiles for not modified books. Only use just before exiting!
void
ml_close_notmod(void) {
   Book   *book;
   FOR_ALL_BOOKS(book) {
      if (!doWasBookChanged(book))
         ml_close(book, TRUE);    // close all not-modified books
   } 
}

// Update the timestamp in the .swp file. Used when the file has been written.
void
ml_timestamp(Book *book) {
   updateBlock0(book, UB_FNAME);
}

// Return FAIL when the ID of "b0p" is wrong.
private int
ml_check_b0_id(Block0 *b0p) {
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
updateBlock0(Book *book, upd_block0_T what) {
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
   mf_put(mfp, hdr, TRUE, FALSE);
}

//Write file name and timestamp into block 0 of a swap file. Also set book->modifiedTime.
//Don't use NameBuff[]!!!
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
      home_replace(NULL, book->fullFileName, b0p->b0_fname, B0_FNAME_SIZE_CRYPT, TRUE);
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
            mch_memmove(b0p->b0_fname + ulen + 1, b0p->b0_fname + 1, flen);
            mch_memmove(b0p->b0_fname + 1, uname, ulen);
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
set_b0_dir_flag(Block0 *b0p, Book *book) {
   if (same_directory(book->mem.mfile->fName, book->fullFileName))
      b0p->b0_flags |= B0_SAME_DIR;
   else
      b0p->b0_flags &= ~B0_SAME_DIR;
}


#include <sys/sysinfo.h>

// Return TRUE if the process with number "b0p->b0_pid" is still running.
// "swap_fname" is the name of the swap file, if it's from before a reboot then
// the result is FALSE;
private int
swapfile_process_running(Block0 *b0p, Byte *swap_fname UNUSED) {
   FileStat       st;
   struct sysinfo  sinfo;

   //If the system rebooted after when the swap file was written then the
   //process can't be running now.
   if (stat((char *)swap_fname, &st) != -1
          && sysinfo(&sinfo) == 0
          && st.st_mtime 
             < time(NULL) - (overrideSysinfoUptimeG >= 0 ? overrideSysinfoUptimeG : sinfo.uptime)
   )
      return FALSE;
   return mch_process_running(charToLong(b0p->b0_pid));
}

// Try to recover curBook from the .swp file.
// If "checkext" is TRUE, check the extension and detect whether it is a swap file.
void
ml_recover(int checkext) {
   Book   *book = NULL;
   MemFile   *mfp = NULL;
   Byte   *fname;
   Byte   *fname_used = NULL;
   BlockHeader   *hdr = NULL;
   Block0   *b0p;
   PointerBlock   *pp;
   DataBlock   *block;
   InfoPtr   *ip;
   BlockId   bnum;
   int      pageCount;
   FileStat   org_stat, swp_stat;
   int      len;
   int      directly;
   LineNr   lnum;
   Byte   *p;
   int      i;
   long   error;
   int      cannot_open;
   LineNr   lineCount;
   int      has_error;
   int      idx;
   int      top;
   int      txt_start;
   FileSize   size;
   int      called_from_main;
   int      serious_error = TRUE;
   long   mtime;
   int      orig_file_status = NOTDONE;

   recoveryModeG = true;
   called_from_main = (curBook->mem.mfile == NULL);
   char deco = getDecoFlags(HLF_E);

   //If the file name ends in ".s[a-w][a-z]" we assume this is the swap file.
   //Otherwise a search is done to find the swap file(s).
   fname = curBook->currFileName;
   if (fname == NULL)          // When there is no file name
      fname = (CS)"";
   len = (int)STRLEN(fname);
   if (checkext && len >= 4 && STRNICMP(fname + len - 4, ".s", 2) == 0
      && firstOccurrence((CS)"abcdefghijklmnopqrstuvw", TOLOWER_ASC(fname[len - 2])) != NULL
      && ASCII_ISALPHA(fname[len - 1])
   ) {
      directly = TRUE;
      fname_used = copyStr(fname); // make a copy for mf_open()
   } else {
      directly = FALSE;

      // count the number of matching swap files
      len = recover_names(fname, FALSE, NULL, 0, NULL);
      if (len == 0) {  // no swap files found
         showErrFmtMsg(_(e_no_swap_file_found_for_str), fname);
         goto theend;
      }
      if (len == 1)          // one swap file found, use it
         i = 1;
      else {    // several swap files found, choose
         // list the names of the swap files
         (void)recover_names(fname, TRUE, NULL, 0, NULL);
         msg_putchar('\n');
         msg_puts(_("Enter number of swap file to use (0 to quit): "));
         i = get_number(FALSE, NULL);
         if (i < 1 || i > len)
            goto theend;
      }
      // get the swap file name that will be used
      (void)recover_names(fname, FALSE, NULL, i, &fname_used);
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
   p = copyStr(fname_used); // save "fname_used" for the message:
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
      if ((size = lseek(mfp->fd, (FileSize)0L, SEEK_END)) <= 0)
          mfp->mf_blocknr_max = 0;       // no file or empty file
      else
          mfp->mf_blocknr_max = (BlockId)(size / mfp->pageSize);
      mfp->pagesInFile = mfp->mf_blocknr_max;

      // need to reallocate the memory used to store the data
      p = alloc(mfp->pageSize);
      if (p == NULL)
          goto theend;
      mch_memmove(p, hdr->bh_data, previous_page_size);
      eeglFree(hdr->bh_data);
      hdr->bh_data = p;
      b0p = (Block0 *)(hdr->bh_data);
   }

   // If .swp file name given directly, use name from swap file for book.
   if (directly) {
      doExpandEnv(OUT filenameBuilder, b0p->b0_fname);
      if (setfname(curBook, NameBuff, NULL, TRUE) == FAIL)
          goto theend;
   }

   home_replace(NULL, mfp->fName, NameBuff, MAXPATHL, TRUE);
   smsg(_("Using swap file \"%s\""), NameBuff);

   if (bookSpName(curBook) != NULL)
      copySubstrToAllocation(NameBuff, (Text){bookSpName(curBook), MAXPATHL - 1});
   else
      home_replace(NULL, curBook->fullFileName, NameBuff, MAXPATHL, TRUE);
   smsg(_("Original file \"%s\""), NameBuff);
   msg_putchar('\n');

   // check date of swap file and original file
   mtime = charToLong(b0p->b0_mtime);
   if (curBook->fullFileName != NULL
          && stat((char *)curBook->fullFileName, &org_stat) != -1
          && ((stat((char *)mfp->fName, &swp_stat) != -1
             && org_stat.st_mtime > swp_stat.st_mtime)
         || org_stat.st_mtime != mtime))
      emsg(_(e_warning_original_file_may_have_been_changed));
   out_flush();


   mf_put(mfp, hdr, FALSE, FALSE);   // release block 0
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

   unchanged(curBook, TRUE);

   bnum = 1;      // start with block 1
   pageCount = 1;   // which is 1 page
   lnum = 0;      // append after line 0 in curBook
   lineCount = 0;
   idx = 0;      // start with first index in block 1
   error = 0;
   book->mem.ml_stack_top = 0;
   book->mem.ml_stack = NULL;
   book->mem.ml_stack_size = 0;   // no stack yet

   if (curBook->fullFileName == NULL)
      cannot_open = TRUE;
   else
      cannot_open = FALSE;

   serious_error = FALSE;
   for ( ; !gotInterruptG; line_breakcheck()) {
      if (hdr)
         mf_put(mfp, hdr, FALSE, FALSE);   // release previous block

      // get block
      if ((hdr = mf_get(mfp, bnum, pageCount)) == NULL) {
         if (bnum == 1) {
            showErrFmtMsg(_(e_unable_to_read_block_one_from_str), mfp->fName);
            goto theend;
         }
         ++error;
         ml_append(lnum++, (CS)_("???MANY LINES MISSING"), (ColNr)0, TRUE);
      } else   {// there is a block
         pp = (PointerBlock *)(hdr->bh_data);
         if (pp->id == PTR_ID) { // it is a pointer block
            int PointerBlockock_error = FALSE;
            if (pp->pointerCountMax != pointerCountMax(mfp)) {
                PointerBlockock_error = TRUE;
                pp->pointerCountMax = pointerCountMax(mfp);
            }
            if (pp->pointerCount > pp->pointerCountMax) {
                PointerBlockock_error = TRUE;
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
                  ml_append(lnum++, (CS)_("???LINE COUNT WRONG"), (ColNr)0, TRUE);
               }
            }

            if (pp->pointerCount == 0) {
               ml_append(lnum++, (CS)_("???EMPTY BLOCK"), (ColNr)0, TRUE);
               ++error;
            } ei (idx < (int)pp->pointerCount) {// go a block deeper
               if (pp->c[idx].blockId < 0) {
                  // Data block with negative block number. Try to read lines from the original file.
                  // This is slow, but it works.
                  if (!cannot_open) {
                     lineCount = pp->c[idx].lineCount;
                     if (readfile(
                                curBook->fullFileName, NULL, lnum, pp->c[idx].oldLnum - 1, lineCount, 
                                NULL, 0
                           ) != OK)
                        cannot_open = TRUE;
                     else
                        lnum += lineCount;
                  }
                  if (cannot_open) {
                      ++error;
                      ml_append(lnum++, (CS)_("???LINES MISSING"), (ColNr)0, TRUE);
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
               ml_append(lnum++, (CS)_("???BLOCK MISSING"), (ColNr)0, TRUE);
            } else {
               //It is a data block. Append all the lines in this block.
               has_error = FALSE;

               // Check the length of the block.
               // If wrong, use the length given in the pointer block.
               if (pageCount * mfp->pageSize != block->endByte) {
                  ml_append(lnum++, 
                     (CS)_("??? from here until ???END lines may be messed up"),
                     (ColNr)0, TRUE
                  );
                  ++error;
                  has_error = TRUE;
                  block->endByte = pageCount * mfp->pageSize;
               }

               // Make sure there is a ZERO at the end of the block so we
               // don't go over the end when copying text.
               *((CS)block + block->endByte - 1) = ZERO;

               // Check the number of lines in the block.
               // If wrong, use the count in the data block.
               if (lineCount != block->countLines) {
                  ml_append(lnum++, 
                  (CS)_("??? from here until ???END" " lines may have been inserted/deleted"),
                               (ColNr)0, TRUE
               );
               ++error;
               has_error = TRUE;
               }

               int did_questions = FALSE;
               for (i = 0; i < block->countLines; ++i) {
                  if ((CS)&(block->c[i]) >= (CS)block + block->startByte) {
                      // line count must be wrong
                      ++error;
                      ml_append(lnum++,
                         (CS)_("??? lines may be missing"),
                                   (ColNr)0, TRUE);
                      break;
                  }

                  txt_start = (block->c[i] & c_MASK);
                  if (txt_start <= (int)HEADER_SIZE || txt_start >= (int)block->endByte) {
                     ++error;
                     // avoid lots of lines with "???"
                     if (did_questions)
                        continue;
                     did_questions = TRUE;
                     p = (CS)"???";
                  } else {
                     did_questions = FALSE;
                     p = (CS)block + txt_start;
                  }
                  ml_append(lnum++, p, (ColNr)0, TRUE);
               }
               if (has_error)
                  ml_append(lnum++, (CS)_("???END"), (ColNr)0, TRUE);
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
      // Recovering an empty file results in two lines and the first line is
      // empty.  Don't set the modified flag then.
      if (!(curBook->mem.lineCount == 2 && *ml_get(1) == ZERO)) {
          changed_internal();
          ++CHANGEDTICK(curBook);
      }
   } else {
      for (idx = 1; idx <= lnum; ++idx) {
         // Need to copy one line, fetching the other one may flush it.
         p = copySubstr(ml_get(idx), ml_get_len(idx));
         i = STRCMP(p, ml_get(idx + lnum));
         eeglFree(p);
         if (i != 0) {
            changed_internal();
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
   redraw_curbuf_later(UPD_NOT_VALID);

theend:
   eeglFree(fname_used);
   recoveryModeG = false;
   if (mfp) {
      if (hdr)
          mf_put(mfp, hdr, FALSE, FALSE);
      mf_close(mfp, FALSE);       // will also eeglFree(mfp->fName)
   }
   if (book) {
      eeglFree(book->mem.ml_stack);
      eeglFree(book);
   }
   if (serious_error && called_from_main)
      ml_close(curBook, TRUE);
   else {
      apply_autocmds(EVENT_BUFREADPOST, NULL, curBook->currFileName, false, curBook);
      apply_autocmds(EVENT_BUFWINENTER, NULL, curBook->currFileName, false, curBook);
   }
}

// Find the names of swap files in current directory and the directory given
// with the 'directory' option.
//
// Used to:
// - list the swap files for "eegl -r"
// - count the number of swap files when recovering
// - list the swap files when recovering
// - list the swap files for swapfilelist()
// - find the name of the n'th swap file when recovering
int
recover_names(
   CS fname,      // base for swap file name
   int do_list,   // when TRUE, list the swap file names
   List* ret_list, // when not NULL add file names to it
   int nr,      // when non-zero, return nr'th swap file name
   OUT CS* fname_out   // result when "nr" > 0
){
   int num_names;
   CS names[6];
   CS tail;
   CS p;
   int num_files;
   ExpandMatch files = {};
   files.a = createArena();
   CS dirp;
   CS dir_name;
   CS fname_res = NULL;
   Byte fnameBuilder[MAXPATHL];

   if (fname) {
      //Expand symlink in the file name, because the swap file is created
      //with the actual file instead of with the symlink.
      if (resolve_symlink(fname, fnameBuilder) == OK)
         fname_res = fnameBuilder;
      else
         fname_res = fname;
   }

   if (do_list) {
      //use msg() to start the scrolling properly
      msg(_("Swap files found:"));
      msg_putchar('\n');
   }

   //Do the loop for every directory in 'directory'.
   //First allocate some memory to put the directory name in.
   dir_name = alloc(STRLEN(p_dir) + 1);
   dirp = p_dir;
   while (dir_name && *dirp) {
      //Isolate a directory name from *dirp and put it into dir_name (we know
      //it is large enough, so use 31000 for length). Advance dirp to next directory name.
      (void)copy_option_part(&dirp, dir_name, 31000, ",");

      if (dir_name[0] == '.' && dir_name[1] == ZERO) { //check current dir
         if (fname == NULL) {
            names[0] = copyStr((CS)"*.sw?");
            //Names starting with a dot are special.
            names[1] = copyStr((CS)".*.sw?");
            names[2] = copyStr((CS)".sw?");
            num_names = 3;
         } else
            num_names = recoverFileNames(names, fname_res, TRUE);
      } else { //check directory dir_name
         if (!fname) {
            names[0] = concat_fnames(dir_name, (CS)"*.sw?", TRUE);
            //Names starting with a dot are special.
            names[1] = concat_fnames(dir_name, (CS)".*.sw?", TRUE);
            names[2] = concat_fnames(dir_name, (CS)".sw?", TRUE);
            num_names = 3;
         } else {
            int   len = (int)STRLEN(dir_name);

            p = dir_name + len;
            if (after_pathsep(dir_name, p) && len > 1 && p[-1] == p[-2]) {
               // Ends with '//', Use Full path for swap name
               tail = make_percent_swname(dir_name, p, fname_res);
            } else {
               tail = gettail(fname_res);
               tail = concat_fnames(dir_name, tail, TRUE);
            }
            if (tail == NULL)
               num_names = 0;
            else {
               num_names = recoverFileNames(names, tail, FALSE);
               eeglFree(tail);
            }
         }
      }

      // check for out-of-memory
      for (int i = 0; i < num_names; ++i) {
         if (names[i] == NULL) {
            for (i = 0; i < num_names; ++i)
               eeglFree(names[i]);
            num_names = 0;
         }
      }
      if (num_names == 0)
          num_files = 0;
      ei (expand_wildcards(num_names, names, EW_NOTENV|EW_KEEPALL|EW_FILE|EW_SILENT, OUT &files) 
            == FAIL)
         num_files = 0;

      //When no swap file found, wildcard expansion might have failed (e.g.
      //not able to execute the shell).
      //Try finding a swap file by simply adding ".swp" to the file name.
      if (*dirp == ZERO && files.len + num_files == 0 && fname != NULL) {
         FileStat       st;
         Byte       *swapname;

         swapname = modname(fname_res, (CS)".swp", TRUE);
         if (swapname) {
            if (stat((char *)swapname, &st) != -1) {   // It exists!
               files.c = ALLOC_ONE(CS);
               if (files.c) { 
                  files.c[0] = swapname;
                  swapname = NULL;
                  num_files = 1;
               }
            }
            eeglFree(swapname);
         }
      }

      //Remove swapfile name of the current book, it must be ignored.
      //But keep it for swapfilelist().
      if (curBook->mem.mfile && (p = curBook->mem.mfile->fName) != NULL && !ret_list) {
         for (int i = 0; i < num_files; ++i) {
            // Do not expand wildcards
            if (fullpathcmp(p, files.c[i], TRUE, FALSE) & FPC_SAME) {
               files.len--;
               if (files.len > 0) {
                  for ( ; i < (int)files.len; ++i)
                      files.c[i] = files.c[i + 1];
               } 
            }
         }
      }
      if (nr > 0) {
         files.len += num_files;
         if (nr <= (int)files.len)  {
            *fname_out = copyStr(files.c[nr - 1 + num_files - files.len]);
            dirp = Em;          // stop searching
         }
      } ei (do_list) {
         if (dir_name[0] == '.' && dir_name[1] == ZERO) {
            if (!fname)
               msg_puts(_("   In current directory:\n"));
            else
               msg_puts(_("   Using specified name:\n"));
         } else {
            msg_puts(_("   In directory "));
            msg_home_replace(dir_name);
            msg_puts(S":\n");
         }

         if (num_files) {
            for (int i = 0; i < num_files; ++i) {
               // print the swap file name
               msg_outnum((long)++files.len);
               msg_puts(S".    ");
               msg_puts(gettail(files.c[i]));
               msg_putchar('\n');
               (void)swapfile_info(files.c[i]);
            }
         } else
            msg_puts(_("      -- none --\n"));
         out_flush();
     } ei (ret_list != NULL) {
         for (int i = 0; i < num_files; ++i) {
            Byte *name = concat_fnames(dir_name, files.c[i], TRUE);
            if (name) {
               list_append_string(ret_list, name, -1);
               eeglFree(name);
            }
         }
      } else
         files.len += num_files;

      for (int i = 0; i < num_names; ++i)
          eeglFree(names[i]);
      deleteArena(files.a);
   }
   eeglFree(dir_name);
   return files.len;
}

//Need _very_ long file names.
//Append the full path to name with path separators made into percent
//signs, to "dir". An unnamed book is handled as "" (<currentdir>/"")
//The last character in "dir" must be an extra slash or backslash, it is removed.
CS
make_percent_swname(Byte *dir, Byte *dir_end, Byte *name) {
   Byte *d = NULL;

   CS f = FullName_save(name != NULL ? name : (CS)"", true);

   CS s = alloc(STRLEN(f) + 1);
   if (s) {
      STRCPY(s, f);
      for (d = s; *d != ZERO; MB_PTR_ADV(d)) {
         if (*d == '/')
            *d = '%';
      } 

      dir_end[-1] = ZERO;  // remove one trailing slash
      d = concat_fnames(dir, s, TRUE);
      eeglFree(s);
   }
   eeglFree(f);
   return d;
}

#define HAVE_PROCESS_STILL_RUNNING
private int process_still_running;

// Return information found in swapfile "fname" in dictionary "d".
// This is used by the swapinfo() function.
void
get_b0_dict(CS fname, Bag *bag) {
   int fd;
   Block0 b0;

   if ((fd = open((char *)fname, O_RDONLY | O_EXTRA, 0)) >= 0) {
      if (read_eintr(fd, &b0, sizeof(b0)) == sizeof(b0)) {
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

// Give information about an existing swap file. Return timestamp (0 when unknown).
private Tyme
swapfile_info(CS fname) {
   FileStat       st;
   Byte       uname[B0_UNAME_SIZE];

   // print the swap file date
   if (stat((char *)fname, &st) != -1) {
      // print name of owner of the file
      if (mch_get_uname(st.st_uid, uname, B0_UNAME_SIZE) == OK) {
         msg_puts(_("          owned by: "));
         msg_outtrans(uname);
         msg_puts(_("   dated: "));
      } else
         msg_puts(_("             dated: "));
      msg_puts(get_ctime(st.st_mtime, TRUE));
   } else
      st.st_mtime = 0;

   // print the original file name
   int fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
   Block0   b0;
   if (fd >= 0) {
      if (read_eintr(fd, &b0, sizeof(b0)) == sizeof(b0)) {
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
#ifdef HAVE_PROCESS_STILL_RUNNING
                  process_still_running = TRUE;
#endif
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

// Return TRUE if the swap file looks OK and there are no changes, thus it can be safely deleted.
private int
swapfile_unchanged(CS fname) {
   FileStat       st;
   int          fd;
   Block0   b0;
   int          ret = TRUE;

   // must be able to stat the swap file
   if (stat((char *)fname, &st) == -1)
      return FALSE;

   // must be able to read the first block
   fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
   if (fd < 0)
      return FALSE;
   if (read_eintr(fd, &b0, sizeof(b0)) != sizeof(b0)) {
      close(fd);
      return FALSE;
   }

   // the ID and magic number must be correct
   if (ml_check_b0_id(&b0) == FAIL|| b0_magic_wrong(&b0))
      ret = FALSE;

   // must be unchanged
   if (b0.b0_dirty)
      ret = FALSE;

   // Host name must be known and must equal the current host name, otherwise
   // comparing pid is meaningless.
   if (*(b0.b0_hname) == ZERO) {
      ret = FALSE;
   } else {
      Byte hostname[B0_HNAME_SIZE];

      mch_get_host_name(hostname, B0_HNAME_SIZE);
      hostname[B0_HNAME_SIZE - 1] = ZERO;
      b0.b0_hname[B0_HNAME_SIZE - 1] = ZERO; // in case of corruption
      if (caseInsensitiveCompare(b0.b0_hname, hostname) != 0)
         ret = FALSE;
   }

   // process must be known and not be running
   if (charToLong(b0.b0_pid) == 0L || swapfile_process_running(&b0, fname))
      ret = FALSE;

   // We do not check the user, it should be irrelevant for whether the swap file is still useful
   close(fd);
   return ret;
}

private int
recoverFileNames(Byte **names, Byte *path, int prepend_dot) {
   //  maybe short name, maybe not: Try both. Only use the short name if it is different.
   Byte   *p;
   int      i;

   int num_names = 0;

   // May also add the file name with a dot prepended, for swap file in same dir as original file.
   if (prepend_dot) {
      names[num_names] = modname(path, (CS)".sw?", TRUE);
      if (names[num_names] == NULL)
         goto end;
      ++num_names;
   }

   // Form the normal swap file name pattern by appending ".sw?".
   names[num_names] = concat_fnames(path, (CS)".sw?", FALSE);
   if (names[num_names] == NULL)
      goto end;
   if (num_names >= 1)   {    // check if we have the same name twice
      p = names[num_names - 1];
      i = (int)STRLEN(names[num_names - 1]) - (int)STRLEN(names[num_names]);
      if (i > 0)
         p += i;       // file name has been expanded to full path

      if (STRCMP(p, names[num_names]) != 0)
         ++num_names;
      else
         eeglFree(names[num_names]);
   } else
      ++num_names;

   if (names[num_names] == NULL)
      goto end;

   p = names[num_names];
   i = STRLEN(names[num_names]) - STRLEN(names[num_names - 1]);
   if (i > 0)
      p += i;      // file name has been expanded to full path
   if (STRCMP(names[num_names - 1], p) == 0)
      eeglFree(names[num_names]);
   else
      ++num_names;

end:
    return num_names;
}

// sync all memlines
//
// If 'check_file' is TRUE, check if original file exists and was not changed.
// If 'check_char' is TRUE, stop syncing when character becomes available, but
// always sync at least one block.
void
ml_sync_all(int check_file, int check_char) {
   Book* book;
   FileStat st;

   FOR_ALL_BOOKS(book) {
      if (book->mem.mfile == NULL
            || book->mem.mfile->fName == NULL
            || book->mem.mfile->fd < 0
      )
         continue;             // no file

      flushLine(book); // flush buffered line
                      // flush locked block
      (void)ml_find_line(book, (LineNr)0, ML_FLUSH);
      if (doWasBookChanged(book) 
            && check_file && mf_need_trans(book->mem.mfile)
            && book->fullFileName != NULL
      ){
         //If the original file does not exist anymore or has been changed
         //call ml_preserve() to get rid of all negative numbered blocks.
         if (stat((char *)book->fullFileName, &st) == -1
             || st.st_mtime != book->readTime
#ifdef ST_MTIM_NSEC
             || st.ST_MTIM_NSEC != book->readTimeNs
#endif
             || st.st_size != book->origSize)
          {
            ml_preserve(book, FALSE);
            did_check_timestamps = FALSE;
            need_check_timestamps = TRUE;   // give message later
         }
      }
      if (book->mem.mfile->mf_dirty == MF_DIRTY_YES) {
         (void)mf_sync(book->mem.mfile, (check_char ? MFS_STOP : 0)
                  | (doWasBookChanged(book) ? MFS_FLUSH : 0));
         if (check_char && ui_char_avail())   // character available now
            break;
      }
   }
}

// Sync one book, including negative blocks. After this all the blocks are in the swap file
// Used for the :preserve command and when the original file has been changed or deleted.
// when message is TRUE the success of preserving is reported
void
ml_preserve(Book* book, int message) {
   BlockHeader   *hdr;
   LineNr   lnum;
   MemFile   *mfp = book->mem.mfile;
   int      status;
   int      gotInterruptG_save = gotInterruptG;

   if (!mfp || !mfp->fName) {
      if (message)
         emsg(_(e_cannot_preserve_there_is_no_swap_file));
      return;
   }

   // We only want to stop when interrupted here, not when interrupted before.
   gotInterruptG = FALSE;

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
CS
ml_get(LineNr lnum) {
   return memGetLine(curBook, lnum, false);
}

// Return pointer to position "pos".
CS
ml_get_pos(Pos *pos){
   return (memGetLine(curBook, pos->lnum, false) + pos->col);
}

// Return pointer to cursor line.
CS
ml_get_curline(void) {
   return memGetLine(curBook, curPor->cursor.lnum, false);
}

// Return pointer to cursor position.
CS
ml_get_cursor(void) {
   return memGetLine(curBook, curPor->cursor.lnum, false) + curPor->cursor.col;
}

// return length (excluding the ZERO) of the given line
ColNr
ml_get_len(LineNr lnum) {
   return memGetBookLen(curBook, lnum);
}

// return length (excluding the ZERO) of the text after position "pos"
ColNr
ml_get_pos_len(Pos *pos) {
   return memGetBookLen(curBook, pos->lnum) - pos->col;
}

// return length (excluding the ZERO) of the cursor line
ColNr
ml_get_curline_len(void) {
   return memGetBookLen(curBook, curPor->cursor.lnum);
}

// return length (excluding the ZERO) of the cursor position
ColNr
ml_get_cursor_len(void) {
   return memGetBookLen(curBook, curPor->cursor.lnum) - curPor->cursor.col;
}

// return length (excluding the ZERO) of the given line in the given book
ColNr
memGetBookLen(Book* book, LineNr lnum) {
   CS line = memGetLine(book, lnum, false); if (*line == ZERO)
      return 0;

   if (book->mem.lineTextLen <= 0)
      book->mem.lineTextLen = (int)STRLEN(line) + 1;
   return book->mem.lineTextLen - 1;
}

//Return a pointer to a line in a specific book
//"willChange": if TRUE mark the book dirty (chars in the line are expected to change)
CS
memGetLine(
   Book* book,
   LineNr   lnum,
   Boole  willChange // line will be changed
){
   BlockHeader* hdr;
   DataBlock   *block;
   static int   recursive = 0;
   static Byte questions[4];

   if (lnum > book->mem.lineCount) { // invalid line number
      if (recursive == 0) {
         // Avoid giving this message for a recursive call, may happen when
         // the GUI redraws part of the text.
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
      return Em;
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
            // Avoid giving this message for a recursive call, may happen
            // when the UI redraws part of the text.
            ++recursive;
            get_trans_bufname(book);
            shorten_dir(NameBuff);
            internalErrFmtMsg(
               e_ml_get_cannot_find_line_nr_in_buffer_nr_str, lnum, book->fiNum, NameBuff
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
int
ml_line_alloced(void) {
   return (curBook->mem.flags & ML_LINE_DIRTY);
}

//Add text properties that continue from the previous line.
private void
addTextPropsForAppend(
   Book* book,
   LineNr   lnum,
   Arr(Byte)* lineContent,
   int* len,
   Byte   **tofree
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
            mch_memmove(newLineContent, *lineContent, *len);
         }
         newPropCount = 0;
      }

      // Get the line above to find any props that continue in the next line.
      CS props;
      count = get_text_props(OUT &props, book, lnum, false);
      for (n = 0; n < count; ++n) {
         mch_memmove(&prop, props + n * sizeof(TextProp), sizeof(TextProp));
         if (prop.flags & TEXT_PROP_CONT_NEXT) {
            if (round == 2) {
               prop.flags |= TEXT_PROP_CONT_PREV;
               prop.col = 1;
               prop.len = *len;  // not exactly the right length
               mch_memmove(
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
   Book   *book,
   LineNr   lnum,      // append after this line (can be 0)
   Arr(Byte) newContentArg, // text of the new line
   ColNr   lenArgWithZeroChar,   // length of line, including ZERO, or 0
   int      flags      // ML_APPEND_ flags
){
   Arr(Byte) newContent = newContentArg;
   ColNr   len = lenArgWithZeroChar;
   int      i;
   int      lineCount;   // number of indexes in current block
   int      offset;
   int      from, to;
   int      neededSpace; // space needed for new line
   int      page_size;
   int      pageCount;
   int      oldLineInd;   // index for lnum in data block
   BlockHeader* hdr;
   PointerBlock* pp;
   InfoPtr* ip;
   Byte* tofree = NULL;
   ColNr  textLen = 0;   // text len with ZERO without text properties
   int   ret = FAIL;

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
      addTextPropsForAppend(book, lnum, &newContent, &len, &tofree);
   }

   neededSpace = len + INDEX_SIZE;   // space needed for text + index

   MemFile* mfp = book->mem.mfile;
   page_size = mfp->pageSize;

   // Find the data block containing the previous line. This also fills the stack with the blocks
   // from the root to the data block, and releases any locked block.
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
         mch_memmove(
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
         mch_memmove((char *)block + block->c[newLineInd], newContent, (Unt)len);
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
          in_left = TRUE;
          // neededSpace does not change
       } else { // left block is existing, right block is new
         lines_moved = lineCount - oldLineInd - 1;
         if (lines_moved == 0)
            in_left = FALSE;   // put new line in right block. neededSpace does not change
         else {
            data_moved = ((block->c[oldLineInd]) & c_MASK) - block->startByte;
            total_moved = data_moved + lines_moved * INDEX_SIZE;
            if ((int)block->freeSpace + total_moved >= neededSpace) {
               in_left = TRUE;   // put new line in left block
               neededSpace = total_moved;
            } else {
               in_left = FALSE;       // put new line in right block
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
            mch_memmove((char *)rightBlock + rightBlock->startByte, newContent, (Unt)len);
         }
         ++lineCount_right;
      }
      // may move lines from the left/old block to the right/new one.
      if (lines_moved) {
         rightBlock->startByte -= data_moved;
         rightBlock->freeSpace -= total_moved;
         mch_memmove((char *)rightBlock + rightBlock->startByte,
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
             mch_memmove((char *)leftBlock + leftBlock->startByte, newContent, (Unt)len);
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
      mf_put(mfp, newBlock, TRUE, FALSE);

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
            mf_put(mfp, hdr, FALSE, FALSE);
            goto theend;
         }
         //TODO: If the pointer block is full and we are adding at the end
         //try to insert in front of the next block
         // block not full, add one entry
         if (pp->pointerCount < pp->pointerCountMax) {
            if (idx + 1 < (int)pp->pointerCount) {
                mch_memmove(&pp->c[idx + 2], &pp->c[idx + 1], (Unt)(pp->pointerCount - idx - 1) * sizeof(PtrEntry));
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

            mf_put(mfp, hdr, TRUE, FALSE);
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
            mch_memmove(pp_new, pp, (Unt)page_size);
            pp->pointerCount = 1;
            pp->c[0].blockId = newBlock->bh_bnum;
            pp->c[0].lineCount = book->mem.lineCount;
            pp->c[0].oldLnum = 1;
            pp->c[0].pageCount = 1;
            mf_put(mfp, hdr, TRUE, FALSE);   // release block 1
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
            mch_memmove(
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
         mf_put(mfp, hdr, TRUE, FALSE);
         mf_put(mfp, newBlock, TRUE, FALSE);
      }

      // Safety check: fallen out of for loop?
      if (stack_idx < 0) {
         internalErrMsg(e_updated_too_many_blocks);
         book->mem.ml_stack_top = 0;   // invalidate stack
      }
   }//}}}

   // The line was inserted below 'lnum'
   updateChunk(book, lnum + 1, (long)textLen, ML_CHNK_ADDLINE);

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
   may_invoke_listeners(book, lnum + 1, lnum + 1, 1);
   if (book->mem.ml_line_lnum != 0)
      flushLine(book);
   return insertLineText(book, lnum, newContent, len, flags);
}

//Append a line to curBook after lnum (may be 0 to insert a line in front of the file).
//"line" does not need to be allocated, but can't be another line in a
//book, unlocking may make it invalid.
//
//"newfile": TRUE when starting to edit a new file, meaning that oldLnum will be set for 
//recovery.
//
//Check: The caller of this function should probably also call appended_lines().
//return FAIL for failure, OK otherwise
int
ml_append(
   LineNr lnum, // append after this line (can be 0)
   Arr(Byte) newContent, // text of the new line
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

int
ml_append_flags(
   LineNr   lnum,      // append after this line (can be 0)
   Arr(Byte) newContent,      // text of the new line
   ColNr   len,      // length of new line, including ZERO, or 0
   int      flags)      // ML_APPEND_ values
{
   // When starting up, we might still need to create the memfile
   if (curBook->mem.mfile == NULL && bookOpenFromInvo(false, NULL, 0) == FAIL)
      return FAIL;
   return appendFlush(curBook, lnum, newContent, len, flags);
}


//Like ml_append() but for an arbitrary book. The buffer must already have a memline.
//"newfile": TRUE when starting to edit a new file, meaning that oldLnum will be set for recovery.
int
ml_append_buf(
   Book* book,
   LineNr lnum,  // append after this line (can be 0)
   CS line,      // text of the new line
   ColNr len,    // length of new line, including ZERO, or 0
   int newfile)  // flag, see above
{
   if (book->mem.mfile == NULL)
      return FAIL;
   return appendFlush(book, lnum, line, len, newfile ? ML_APPEND_NEW : 0);
}

//Replace line "lnum", with buffering, in current book.
//
//If "copy" is TRUE, make a copy of the line, otherwise the line has been copied to allocated 
//memory already. If "copy" is FALSE the "line" may be freed to add text properties! Do not use 
//it after calling ml_replace().
//Check: The caller of this function should probably also call changed_lines(), unless 
//drawUpdateScreen(UPD_NOT_VALID) is used.
//return FAIL for failure, OK otherwise
int
ml_replace(LineNr lnum, Byte *line, int copy) {
   ColNr len = -1;

   if (line)
      len = (ColNr)STRLEN(line);
   return ml_replace_len(lnum, line, len, FALSE, copy);
}

//Replace a line for the current buffer. Like ml_replace() with: "len_arg" is the length of the 
//text, excluding ZERO. If "has_props" is TRUE then "line_arg" includes the text properties 
//and "len_arg" includes the ZERO of the text and text properties. When "copy" is TRUE copy 
//the text into allocated memory, otherwise "line_arg" must be allocated and will be consumed here.
int
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
            mch_memmove(newline, line, len);
            mch_memmove(newline + len, curBook->mem.cachedLine + oldtextlen, textproplen);
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
   Book       *book,
   LineNr    lnum,
   Byte       *del_props,
   int       del_props_len,
   int       above)
{
   int      did_get_line = FALSE;
   int      done_this;
   TextProp   prop_del;
   BlockHeader   *hdr;
   DataBlock* block;
   int      idx;
   int      line_start;
   long   line_size;
   int      this_props_len = 0;
   Byte   *text;
   Unt   textlen;
   int      found;

   for (int done_del = 0; done_del < del_props_len; done_del += sizeof(TextProp)) {
      mch_memmove(&prop_del, del_props + done_del, sizeof(TextProp));
      if ((above && (prop_del.flags & TEXT_PROP_CONT_PREV)
             && !(prop_del.flags & TEXT_PROP_CONT_NEXT))
          || (!above && (prop_del.flags & TEXT_PROP_CONT_NEXT) 
             && !(prop_del.flags & TEXT_PROP_CONT_PREV))
      ){
         if (!did_get_line) {
            did_get_line = TRUE;
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

         found = FALSE;
         for (done_this = 0; done_this < this_props_len; done_this += sizeof(TextProp)) {
            int flag = above ? TEXT_PROP_CONT_NEXT : TEXT_PROP_CONT_PREV;
            TextProp  prop_this;

            mch_memmove(&prop_this, text + textlen + done_this, sizeof(TextProp));
            if ((prop_this.flags & flag)
               && prop_del.id == prop_this.id
               && prop_del.type == prop_this.type
            ){
               found = TRUE;
               prop_this.flags &= ~flag;
               mch_memmove(text + textlen + done_this, &prop_this, sizeof(TextProp));
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
   BlockHeader   *hdr;
   DataBlock   *block;
   PointerBlock   *pp;
   InfoPtr   *ip;
   int      count;       // number of entries in block
   int      idx;
   int      stack_idx;
   long   line_size;
   int      i;
   int      ret = FAIL;
   Byte   *textprop_save = NULL;
   long   textprop_len = 0;

   if (lowest_marked && lowest_marked > lnum)
      lowest_marked--;

   // If the file becomes empty the last line is replaced by an empty line.
   if (book->mem.lineCount == 1) {    // file becomes empty
       if ((flags & ML_DEL_MESSAGE)) {
          set_keep_msg((CS)_(no_lines_msg), 0);
       }

       i = ml_replace((LineNr)1, Em, TRUE);
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
            mf_put(mfp, hdr, FALSE, FALSE);
            goto theend;
         }
         count = --(pp->pointerCount);
         if (count == 0)       // the pointer block becomes empty!
            mf_free(mfp, hdr);
         else {
            if (count != idx)   // move entries after the deleted one
                mch_memmove(&pp->c[idx], &pp->c[idx + 1], (Unt)(count - idx) * sizeof(PtrEntry));
            mf_put(mfp, hdr, TRUE, FALSE);

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
      mch_memmove(
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
   if (textprop_save != NULL) {
      // Adjust text properties in the line above and below.
      if (lnum > 1)
         adjustTextPropsForDeletion(book, lnum - 1, textprop_save, (int)textprop_len, TRUE);
      if (lnum <= book->mem.lineCount) {
         adjustTextPropsForDeletion(book, lnum, textprop_save, (int)textprop_len, FALSE);
      }
   }
   eeglFree(textprop_save);
   return ret;
}


// Delete line "lnum" in the current book.
// When "message" is TRUE may give a "No lines in book" message.
//
// Check: The caller of this function should probably also call
// deleted_lines() after this.
//
// return FAIL for failure, OK otherwise
int
ml_delete(LineNr lnum) {
   return ml_delete_flags(lnum, 0);
}

// Delete line "lnum" in the current book. When "message" is TRUE may give a 
// "No lines in book" message.
// Check: The caller of this function should probably also call deleted_lines() after this.
//
// return FAIL for failure, OK otherwise
int
ml_deleteBufLine(Book* book, LineNr lnum) {
   flushLine(book);
   if (lnum < 1 || lnum > book->mem.lineCount)
      return FAIL;

   // When inserting above recorded changes: flush the changes before changing the text.
   may_invoke_listeners(book, lnum, lnum + 1, -1);

   return deleteLine(book, lnum, 0);
}

// Like ml_delete() but using flags (see deleteLine()).
int
ml_delete_flags(LineNr lnum, int flags) {
   flushLine(curBook);
   if (lnum < 1 || lnum > curBook->mem.lineCount)
      return FAIL;

   // When inserting above recorded changes: flush the changes before changing the text.
   may_invoke_listeners(curBook, lnum, lnum + 1, -1);

   return deleteLine(curBook, lnum, flags);
}

// set the DB_MARKED flag for line 'lnum'
void
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
LineNr
ml_firstmarked(void) {
   BlockHeader   *hdr;
   LineNr   lnum;
   int      i;

   if (curBook->mem.mfile == NULL)
      return (LineNr) 0;

   //The search starts with lowest_marked line. This is the last line where
   //a mark was found, adjusted by inserting/deleting lines.
   for (lnum = lowest_marked; lnum <= curBook->mem.lineCount; ) {
      //Find the data block containing the line.
      //This also fills the stack with the blocks from the root to the data
      //block This also releases any locked block.
      if ((hdr = ml_find_line(curBook, lnum, ML_FIND)) == NULL)
         return (LineNr)0;          // give error message?

      DataBlock* block = (DataBlock *)(hdr->bh_data);

      for (i = lnum - curBook->mem.lockedLow; lnum <= curBook->mem.lockedHigh; ++i, ++lnum) {
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
void
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
   Byte   *new_line;
   Byte   *old_line;
   ColNr   new_len;
   int      old_len;
   int      extra;
   int      idx;
   int      start;
   int      count;
   int      i;
   static int  entered = FALSE;

   if (book->mem.ml_line_lnum == 0 || book->mem.mfile == NULL)
      return;      // nothing to do

   if (book->mem.flags & ML_LINE_DIRTY) {
      // This code doesn't work recursively, but Netbeans may call back here
      // when obtaining the cursor position.
      if (entered)
          return;
      entered = TRUE;

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
                mch_memmove((char *)block + block->startByte - extra,
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
            mch_memmove(old_line - extra, new_line, (Unt)new_len);
            book->mem.flags |= (ML_LOCKED_DIRTY | ML_LOCKED_POS);
            // The else case is already covered by the insert and delete
            if (book->hasTextprop) {
                // Do not count the size of any text properties.
                extra += old_prop_len;
                extra -= new_len - (int)STRLEN(new_line) - 1;
            }
            if (extra != 0)
               updateChunk(book, lnum, (long)extra, ML_CHNK_UPDLINE);
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

      entered = FALSE;
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
   if ((hdr = mf_new(mfp, FALSE, 1)) == NULL)
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

      dirty = FALSE;
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
                  dirty = TRUE;
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
         dirty = TRUE;
      } ei (action == ML_INSERT) {
         pp->c[idx].lineCount++;
         dirty = TRUE;
      }
      mf_put(mfp, hdr, dirty, FALSE);
   }

error_block:
   mf_put(mfp, hdr, FALSE, FALSE);
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
         mch_memmove(newstack, book->mem.ml_stack, (Unt)top * sizeof(InfoPtr));
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
         mf_put(mfp, hdr, FALSE, FALSE);
         internalErrMsg(e_pointer_block_id_wrong_two);
         break;
      }
      pp->c[ip->ip_index].lineCount += count;
      ip->ip_high += count;
      mf_put(mfp, hdr, TRUE, FALSE);
   }
}

// Resolve a symlink in the last component of a file name.
// Note that f_resolve() does it for every part of the path, we don't do that here.
// If it worked returns OK and the resolved link in "buf[MAXPATHL]". Otherwise return FAIL.
int
resolve_symlink(CS fname, OUT CS builder) {
   Byte   tmp[MAXPATHL];
   int      ret;
   int      depth = 0;

   if (!fname)
      return FAIL;

   // Put the result so far in tmp[], starting with the original name.
   copySubstrToAllocation(tmp, (Text){fname, MAXPATHL - 1});

   for (;;) {
      // Limit symlink depth to 100, catch recursive loops.
      if (++depth == 100) {
         showErrFmtMsg(_(e_symlink_loop_for_str), fname);
         return FAIL;
      }

      ret = readlink((char *)tmp, (char *)builder, MAXPATHL - 1);
      if (ret <= 0) {
         if (errno == EINVAL || errno == ENOENT) {
            // Found non-symlink or not existing file, stop here.
            // When at the first level use the unmodified name, skip the call to eeFullFileName().
            if (depth == 1)
                return FAIL;

            // Use the resolved name in tmp[].
            break;
         }

         // There must be some error reading links, use original name.
         return FAIL;
      }
      builder[ret] = ZERO;

      // Check whether the symlink is relative or absolute.
      // If it's relative, build a new path based on the directory
      // portion of the filename (if any) and the path the symlink points to.
      if (mch_isFullName(builder))
          STRCPY(tmp, builder);
      else {
         Byte *tail;

         tail = gettail(tmp);
         if (STRLEN(tail) + STRLEN(builder) >= MAXPATHL)
            return FAIL;
         STRCPY(tail, builder);
      }
   }

   //Try to resolve the full name of the file so that the swapfile name will
   //be consistent even when opening a relative symlink from different working directories.
   return eeFullFileName(tmp, builder, MAXPATHL, TRUE);
}

// Make swap file name out of the file name and a directory name. Return pointer to allocated 
// memory or NULL.
CS
makeswapname(CS fname, CS ffname UNUSED, CS dir_name) {
   Byte   *r;
   CS fname_res = fname;
   Byte fnameBuilder[MAXPATHL];

   // Expand symlink in the file name, so that we put the swap file with the
   // actual file instead of with the symlink.
   if (resolve_symlink(fname, fnameBuilder) == OK)
      fname_res = fnameBuilder;

   int len = (int)STRLEN(dir_name);

   CS s = dir_name + len;
   if (after_pathsep(dir_name, s) && len > 1 && s[-1] == s[-2]) {
      // Ends with '//', Use Full path
      r = NULL;
      if ((s = make_percent_swname(dir_name, s, fname_res)) != NULL) {
         r = modname(s, (CS)".swp", FALSE);
         eeglFree(s);
      }
      return r;
   }

   r = buf_modname(
       fname_res,
       (CS)
       ".swp",
       // Prepend a '.' to the swap file name for the current directory.
       dir_name[0] == '.' && dir_name[1] == ZERO
   );
   if (r == NULL)       // out of memory
      return NULL;

   s = get_file_in_dir(r, dir_name);
   eeglFree(r);
   return s;
}

//Get file name to use for swap file or backup file.
//Use the name of the edited file "fname" and an entry in the 'dir' or 'bdir' option "dname".
//- If "dname" is ".", return "fname" (swap file in dir of file).
//- If "dname" starts with "./", insert "dname" in "fname" (swap file
//  relative to dir of file).
//- Otherwise, prepend "dname" to the tail of "fname" (swap file in specific
//  dir).
//
//The return value is an allocated string and can be NULL.
CS
get_file_in_dir(
   CS fname,
   CS dname   // don't use "dirname", it is a global for Alpha
){
   Byte   *t;
   Byte   *retval;

   CS tail = gettail(fname);

   if (dname[0] == '.' && dname[1] == ZERO)
      retval = copyStr(fname);
   ei (dname[0] == '.' && dname[1] == '/') {
      if (tail == fname)       // no path before file name
          retval = concat_fnames(dname + 2, tail, TRUE);
      else {
          int save_char = *tail;
          *tail = ZERO;
          t = concat_fnames(fname, dname + 2, TRUE);
          *tail = save_char;
          if (t == NULL)       // out of memory
         retval = NULL;
          else {
         retval = concat_fnames(t, tail, TRUE);
         eeglFree(t);
          }
      }
   } else
      retval = concat_fnames(dname, tail, TRUE);

   return retval;
}

// Print the ATTENTION message: info about an existing swap file.
private void
attention_message(Book* book, CS swapName) {
   FileStat   st;
   Tyme   swap_mtime;

   ++no_wait_return;
   (void)emsg(_(e_attention));
   msg_puts(_("\nFound a swap file by the name \""));
   msg_home_replace(swapName);
   msg_puts(S"\"\n");
   swap_mtime = swapfile_info(swapName);
   msg_puts(_("While opening file \""));
   msg_outtrans(book->currFileName);
   msg_puts(S"\"\n");
   if (stat((char *)book->currFileName, &st) == -1) {
      msg_puts(_("      CANNOT BE FOUND"));
   } else {
      msg_puts(_("             dated: "));
      msg_puts(get_ctime(st.st_mtime, TRUE));
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

typedef enum {
   SEA_CHOICE_NONE = 0,
   SEA_CHOICE_READONLY = 1,
   SEA_CHOICE_EDIT = 2,
   SEA_CHOICE_RECOVER = 3,
   SEA_CHOICE_DELETE = 4,
   SEA_CHOICE_QUIT = 5,
   SEA_CHOICE_ABORT = 6
} SeaChoice;

//Trigger the SwapExists autocommands. Return a value for equivalent to do_dialog().
private SeaChoice 
do_swapexists(Book* book, Byte *fname) {
   set_EeglVar_string(VV_SWAPNAME, fname, -1);
   set_EeglVar_string(VV_SWAPCHOICE, NULL, -1);

   // Trigger SwapExists autocommands with <afile> set to the file being
   // edited.  Disallow changing directory here.
   ++allBookLock;
   apply_autocmds(EVENT_SWAPEXISTS, book->currFileName, NULL, false, NULL);
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
//Several names are tried to find one that does not exist Returns the name in allocated memory or 
//NULL. When out of memory "dirp" is set to NULL.
//
//Note: If BASENAMELEN is not correct, you will get error messages for not being able to open the 
//swap or undo file. Note: May trigger SwapExists autocmd, pointers may change!
private Byte *
findSwapName(
   Book* book,
   Byte   **dirp,      // pointer to list of directories
   CS old_fname)   // don't give warning for this file name
{
   Byte   *fname;
   int      n;
   CS buf_fname = book->currFileName;

   //Isolate a directory name from *dirp and put it in dir_name. First allocate some memory to 
   //put the directory name in. we try different names until we find one that does not exist yet
   CS dir_name = alloc(STRLEN(*dirp) + 1);
   if (!dir_name) {
      *dirp = NULL;
      fname = NULL;
      goto endOfName;
   } else {
      (void)copy_option_part(dirp, dir_name, 31000, ",");
      fname = makeswapname(buf_fname, book->fullFileName, dir_name);
   } 

   if ((n = (int)STRLEN(fname)) == 0) {// sanity check
      EE_CLEAR(fname);
      goto endOfName;
   }
   //check if the swapfile already exists
   if (mch_getperm(fname) < 0) {// it does not exist
      FileStat   sb;

      //Extra security check: When a swap file is a symbolic link, this
      //is most likely a symlink attack.
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
      if (!recoveryModeG && buf_fname && book->kind != BOOK_SPELL
            && !(book->flags & (BF_DUMMY | BF_NO_SEA))
      ){
         Block0   b0;
         int      differ = FALSE;

         // Try to read block 0 from the swap file to get the original file name (and inode number)
         int fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
         if (fd >= 0) {
            if (read_eintr(fd, &b0, sizeof(b0)) == sizeof(b0)) {
               // If the swapfile has the same directory as the
               // buffer don't compare the directory names, they can have a different mountpoint.
               if (b0.b0_flags & B0_SAME_DIR) {
                  if (fnamecmp(gettail(book->fullFileName), gettail(b0.b0_fname)) != 0
                      || !same_directory(fname, book->fullFileName)
                  ) {
                     // Symlinks may point to the same file even
                     // when the name differs, need to check the inode too.
                     doExpandEnv(OUT filenameBuilder, b0.b0_fname);
                     if (compareFnameWithInode(
                           book->fullFileName, NameBuff, charToLong(b0.b0_ino)
                        )
                     )
                        differ = TRUE;
                  }
               } else {
                  // The name in the swap file may be "~user/path/file".  Expand it first.
                  doExpandEnv(OUT filenameBuilder, b0.b0_fname);
                  if (compareFnameWithInode(book->fullFileName, NameBuff, charToLong(b0.b0_ino)))
                     differ = TRUE;
               }
            }
            close(fd);
         }

         // give the ATTENTION message when there is an old swap file
         // for the current file, and the buffer was not recovered.
         if (differ == FALSE && !(curBook->flags & BF_RECOVERED)
            && firstOccurrence(p_shm, SHM_ATTENTION) == NULL)
         {
            SeaChoice choice = SEA_CHOICE_NONE;
            FileStat    st;
#ifdef HAVE_PROCESS_STILL_RUNNING
            process_still_running = FALSE;
#endif
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
               gotInterruptG = FALSE;

               // If vimrc has "simalt ~x" we don't want it to interfere with the prompt here
               flush_buffers(FLUSH_TYPEAHEAD);
            }

            if (swap_exists_action != SEA_NONE && choice == SEA_CHOICE_NONE) {
               Byte   *name;
               int   dialog_result;
               Unt  len = STRLEN(_("Swap file \""));

               name = alloc(STRLEN(fname) + len + STRLEN(_("\" already exists!")) + 5);
               if (!name) {
                  STRCPY(name, _("Swap file \""));
                  home_replace(NULL, fname, name + len, 1000, TRUE);
                  STRCAT(name, _("\" already exists!"));
               }
               dialog_result = do_dialog(
                     EE_WARNING,
                     (CS)_("EEGL - ATTENTION"),
                     name == NULL ?  (CS)_("Swap file already exists!") : name,
# ifdef HAVE_PROCESS_STILL_RUNNING
                     process_still_running
                     ? (CS)_("&Open Read-Only\n&Edit anyway\n&Recover\n&Quit\n&Abort") :
# endif
                     (CS)_(
                        "&Open Read-Only\n&Edit anyway\n&Recover\n&Delete it\n&Quit\n&Abort"), 1, 
                        NULL, FALSE
                     );

# ifdef HAVE_PROCESS_STILL_RUNNING
               if (process_still_running && dialog_result >= 4)
                  // compensate for missing "Delete it" button
                  dialog_result++;
# endif
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
               gotInterruptG = TRUE;
               break;
            case SEA_CHOICE_NONE:
               msg_puts(S"\n");
               if (msg_silent == 0)
                  // call wait_return() later
                  need_wait_return = TRUE;
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
   eeglFree(dir_name);
   return fname;
}

private int
b0_magic_wrong(Block0 *b0p) {
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
//     == 0   != 0    FAIL    X   TRUE
//     == 0   != 0   X   FAIL   TRUE
//
//current file exists, inode for swap unknown, file name(s) not
//available -> probably different
//     != 0   == 0    FAIL    X   TRUE
//     != 0   == 0   X   FAIL   TRUE
//
//current file doesn't exist, inode for swap unknown, one file name not
//available -> probably different
//     == 0   == 0    FAIL    OK   TRUE
//     == 0   == 0   OK   FAIL   TRUE
//
//current file doesn't exist, inode for swap unknown, both file names not
//available -> compare file names
//     == 0   == 0    FAIL   FAIL   fname_c != fname_s
//
//Note that when the ino_t is 64 bits, only the last 32 will be used.  This
//can't be changed without making the block 0 incompatible with 32 bit versions.
private int
compareFnameWithInode(
   CS fname_c,       // current file name
   CS fname_s,       // file name from swap file
   long ino_block0
){
   FileStat   st;
   ino_t   ino_c = 0;       // ino of current file
   ino_t   ino_s;          // ino of file from swap file
   Byte buf_c[MAXPATHL];    // full path of fname_c
   Byte buf_s[MAXPATHL];    // full path of fname_s
   int      retval_c;       // flag: buf_c valid
   int      retval_s;       // flag: buf_s valid

   if (stat((char *)fname_c, &st) == 0)
      ino_c = (ino_t)st.st_ino;

   //First we try to get the inode from the file name, because the inode in
   //the swap file may be outdated.  If that fails (e.g. this path is not
   //valid on this machine), use the inode from block 0.
   if (stat((char *)fname_s, &st) == 0)
      ino_s = (ino_t)st.st_ino;
   else
      ino_s = (ino_t)ino_block0;

   if (ino_c && ino_s)
      return (ino_c != ino_s);

   //One of the inode numbers is unknown, try a forced eeFullFileName() and compare the file names
   retval_c = eeFullFileName(fname_c, buf_c, MAXPATHL, TRUE);
   retval_s = eeFullFileName(fname_s, buf_s, MAXPATHL, TRUE);
   if (retval_c == OK && retval_s == OK)
      return STRCMP(buf_c, buf_s) != 0;

   //Can't compare inodes or file names, guess that the files are different, unless both appear 
   //not to exist at all, then compare with the file name in the swap file.
   if (ino_s == 0 && ino_c == 0 && retval_c == FAIL && retval_s == FAIL)
      return STRCMP(fname_c, fname_s) != 0;
   return TRUE;
}

//Move a long integer into a four byte character array. Used for machine independency in block zero
private void
longToChar(long n, Byte *s) {
   s[0] = (Byte)(n & 0xff);
   n = (unsigned)n >> 8;
   s[1] = (Byte)(n & 0xff);
   n = (unsigned)n >> 8;
   s[2] = (Byte)(n & 0xff);
   n = (unsigned)n >> 8;
   s[3] = (Byte)(n & 0xff);
}

private long
charToLong(Byte *s) {
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
void
ml_setflags(Book* book) {
   BlockHeader   *hdr;
   Block0   *b0p;

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
updateChunk(
   Book* book,
   LineNr   line,
   long   len,
   int      updtype
){
   static Book   *ml_upd_lastbuf = NULL;
   static LineNr   ml_upd_lastline;
   static LineNr   ml_upd_lastcurline;
   static int      ml_upd_lastcurix;

   LineNr      curline = ml_upd_lastcurline;
   int         curix = ml_upd_lastcurix;
   long      size;
   MemChunkSize      *curchnk;
   int         rest;
   BlockHeader      *hdr;
   DataBlock      *block;

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
         MemChunkSize *t_chunksize = book->mem.ml_chunksize;

         book->mem.ml_numchunks = book->mem.ml_numchunks * 3 / 2;
         book->mem.ml_chunksize = eeRealloc(book->mem.ml_chunksize,
                sizeof(MemChunkSize) * book->mem.ml_numchunks);
         if (book->mem.ml_chunksize == NULL) {
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
         int linecnt;

         mch_memmove(book->mem.ml_chunksize + curix + 1,
            book->mem.ml_chunksize + curix,
            (book->mem.ml_usedchunks - curix) *
            sizeof(MemChunkSize)
         );
         // Compute length of first half of lines in the split chunk
         size = 0;
         linecnt = 0;
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

               // We cannot use the text pointers to get the text length,
               // the text prop info would also be counted.  Go over the lines.
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
         && curchnk->mlcs_numlines + curchnk[1].mlcs_numlines <= MLCS_MINL)
      {
          curix++;
          curchnk = book->mem.ml_chunksize + curix;
      } ei (curix == 0 && curchnk->mlcs_numlines <= 0) {
         book->mem.ml_usedchunks--;
         mch_memmove(
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
          mch_memmove(book->mem.ml_chunksize + curix,
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
long
ml_find_line_or_offset(Book* book, LineNr lnum, long *offp) {
   LineNr   curline;
   int      curix;
   long   size;
   BlockHeader   *hdr;
   DataBlock* block;
   int      count;      // number of entries in block
   int      idx;
   int      start_idx;
   int      textEnd;
   long   offset;
   int      len;
   int      extra = 0;

   // take care of cached line first
   flushLine(curBook);

   if (book->mem.ml_usedchunks == -1 || book->mem.ml_chunksize == NULL || lnum < 0)
      return -1;

   if (offp == NULL)
      offset = 0;
   else
      offset = *offp;
   if (lnum == 0 && offset <= 0)
      return 1;   // Not a "find offset" and offset 0 _must_ be in line 1
   //Find the last chunk before the one containing our line. Last chunk is
   //special because it will never qualify.
   curline = 1;
   curix = size = 0;
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
      start_idx = idx = curline - book->mem.lockedLow;
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
            Byte *p = (CS)block + ((block->c[i]) & c_MASK);
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

// Go to byte in buffer with offset 'cnt'.
void
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

//}}}
//{{{memfile

//memfile.c: Contains the functions for handling blocks of memory which can
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
private BlockHeader *mf_find_hash(MemFile *, BlockId);
private void mf_ins_used(MemFile *, BlockHeader *);
private void mf_rem_used(MemFile *, BlockHeader *);
private BlockHeader *mf_release(MemFile *, int);
private BlockHeader *mf_alloc_bhdr(MemFile *, int);
private void mf_free_bhdr(BlockHeader *);
private void mf_ins_free(MemFile *, BlockHeader *);
private BlockHeader *mf_rem_free(MemFile *);
private int mf_read(MemFile *, BlockHeader *);
private int mf_write(MemFile *, BlockHeader *);
private int mf_write_block(MemFile *mfp, BlockHeader *hp, FileSize offset, unsigned size);
private int mf_trans_add(MemFile *, BlockHeader *);
private void mf_do_open(MemFile *, Byte *, int);
private void mf_hash_init(MfHashTable *);
private void mf_hash_free(MfHashTable *);
private void mf_hash_free_all(MfHashTable *);
private MfHashItem *mf_hash_find(MfHashTable *, BlockId);
private void mf_hash_add_item(MfHashTable *, MfHashItem *);
private void mf_hash_rem_item(MfHashTable *, MfHashItem *);
private int mf_hash_grow(MfHashTable *);

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
MemFile *
mf_open(CS fname, Unt flags) {
   FileSize      size;
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
        || (size = lseek(mfp->fd, (FileSize)0L, SEEK_END)) <= 0)
      mfp->mf_blocknr_max = 0;   // no file or empty file
   else
      mfp->mf_blocknr_max = (BlockId)((size + mfp->pageSize - 1)
                      / mfp->pageSize);
   mfp->mf_blocknr_min = -1;
   mfp->mf_neg_count = 0;
   mfp->pagesInFile = mfp->mf_blocknr_max;

    /*
     * Compute maximum number of pages ('maxmem' is in Kbyte):
     *   'mammem' * 1Kbyte / page-size-in-bytes.
     * Avoid overflow by first reducing page size as much as possible.
     */
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

// Open a file for an existing memfile.  Used when updatecount set from 0 to some value.
// If the file already exists, this fails.
// "fname" is the name of file to use (NULL means no file at all)
// Note: "fname" must have been allocated, it is not copied!  If opening the
// file fails, "fname" is freed.
//
// return value: FAIL if file could not be opened, OK otherwise
int
mf_open_file(MemFile *mfp, Byte *fname) {
   mf_do_open(mfp, fname, O_RDWR|O_CREAT|O_EXCL); // try to open the file

   if (mfp->fd < 0)
      return FAIL;

   mfp->mf_dirty = MF_DIRTY_YES;
   return OK;
}

// Close a memory file and delete the associated file if 'del_file' is TRUE.
void
mf_close(MemFile* mfp, int del_file) {
   if (!mfp)          // safety check
      return;
   if (mfp->fd >= 0 && close(mfp->fd) < 0)
      emsg(_(e_close_error_on_swap_file));
   if (del_file && mfp->fName != NULL)
      mch_remove(mfp->fName); // free entries in used list
      
   BlockHeader *nextp;
   for (BlockHeader* hp = mfp->usedFirst; hp != NULL; hp = nextp) {
      total_mem_used -= (Ulong)hp->pageCount * mfp->pageSize;
      nextp = hp->bh_next;
      mf_free_bhdr(hp);
   }
   while (mfp->freeFirst != NULL)       // free entries in free list
      eeglFree(mf_rem_free(mfp));
   mf_hash_free(&mfp->mf_hash);
   mf_hash_free_all(&mfp->mf_trans);       // free hashtable and its items
   eeglFree(mfp->fName);
   eeglFree(mfp->fullFName);
   eeglFree(mfp);
}

// Close the swap file for a memfile.  Used when 'swapfile' is reset.
void
mf_close_file(
   Book* book,
   int      getlines)   // get all lines into memory?
{
   MemFile   *mfp;
   LineNr   lnum;

   mfp = book->mem.mfile;
   if (!mfp || mfp->fd < 0)      // nothing to close
      return;

   if (getlines) {
      // get all blocks in memory by accessing all lines (clumsy!)
      dontReleaseBlocksS = TRUE;
      for (lnum = 1; lnum <= book->mem.lineCount; ++lnum)
         (void)memGetLine(book, lnum, false);
      dontReleaseBlocksS = FALSE;
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
void
mf_new_page_size(MemFile *mfp, unsigned new_size) {
   // Correct the memory used for block 0 to the new size, because it will be
   // freed with that size later on.
   total_mem_used += new_size - mfp->pageSize;
   mfp->pageSize = new_size;
}

// get a new block
//   negative: TRUE if negative block number desired (data block)
BlockHeader *
mf_new(MemFile *mfp, int negative, int page_count) {
   BlockHeader   *hp;   // new BlockHeader
   BlockHeader   *freep;   // first block in free list
   Byte   *p;

   // If we reached the maximum size for the used memory blocks, release one
   // If a BlockHeader is returned, use it and adjust the page_count if necessary.
   hp = mf_release(mfp, page_count);

   // Decide on the number to use:
   // If there is a free block, use its number.
   // Otherwise use mf_block_min for a negative number, mf_block_max for a positive number.
   freep = mfp->freeFirst;
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
         if (hp == NULL && (hp = mf_alloc_bhdr(mfp, page_count)) == NULL)
            return NULL;
         hp->bh_bnum = freep->bh_bnum;
         freep->bh_bnum += page_count;
         freep->pageCount -= page_count;
      } ei (hp == NULL) {      // need to allocate memory for this block
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
BlockHeader *
mf_get(MemFile *mfp, BlockId nr, int page_count) {
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
      if (hp == NULL && page_count > 0)
          hp = mf_alloc_bhdr(mfp, page_count);
      if (hp == NULL)
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
void
mf_put(
   MemFile   *mfp,
   BlockHeader   *hp,
   int      dirty,
   int      infile)
{
   int flags = hp->bh_flags;

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
void
mf_free(MemFile *mfp, BlockHeader *hp) {
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
int
mf_sync(MemFile *mfp, int flags) {
   int      status;
   BlockHeader   *hp;
   int      gotInterruptG_save = gotInterruptG;

   if (mfp->fd < 0) {
      // there is no file, nothing to do
      mfp->mf_dirty = MF_DIRTY_NO;
      return FAIL;
   }

   // Only a CTRL-C while writing will break us here, not one typed previously.
   gotInterruptG = FALSE;

   //sync from last to first (may reduce the probability of an inconsistent
   //file) If a write fails, it is very likely caused by a full filesystem.
   //Then we only try to write blocks within the existing file. If that also fails then we give up.
   status = OK;
   for (hp = mfp->usedLast; hp != NULL; hp = hp->bh_prev) {
      if (((flags & MFS_ALL) || hp->bh_bnum >= 0)
         && (hp->bh_flags & BH_DIRTY)
         && (status == OK || (hp->bh_bnum >= 0
             && hp->bh_bnum < mfp->pagesInFile)))
      {
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
   if (hp == NULL || status == FAIL)
      mfp->mf_dirty = MF_DIRTY_NO;

   if ((flags & MFS_FLUSH) && *p_sws != ZERO) {
      if (STRCMP(p_sws, "fsync") == 0) {
         if (eeFsync(mfp->fd))
            status = FAIL;
      } else
         sync();
   }

   gotInterruptG |= gotInterruptG_save;

   return status;
}

// For all blocks in memory file *mfp that have a positive block number set the dirty flag. These 
// are blocks that need to be written to a newly created swapfile.
void
mf_set_dirty(MemFile *mfp) {
   for (BlockHeader* hp = mfp->usedLast; hp != NULL; hp = hp->bh_prev) {
      if (hp->bh_bnum > 0)
         hp->bh_flags |= BH_DIRTY;
   } 
   mfp->mf_dirty = MF_DIRTY_YES;
}

// insert block *hp in front of hashlist of memfile *mfp
private void
mf_ins_hash(MemFile *mfp, BlockHeader *hp) {
   mf_hash_add_item(&mfp->mf_hash, (MfHashItem *)hp);
}

// remove block *hp from hashlist of memfile list *mfp
private void
mf_rem_hash(MemFile *mfp, BlockHeader *hp) {
   mf_hash_rem_item(&mfp->mf_hash, (MfHashItem *)hp);
}

// look in hash lists of memfile *mfp for block header with number 'nr'
private BlockHeader *
mf_find_hash(MemFile *mfp, BlockId nr) {
    return (BlockHeader *)mf_hash_find(&mfp->mf_hash, nr);
}

// insert block *hp in front of used list of memfile *mfp
private void
mf_ins_used(MemFile *mfp, BlockHeader *hp) {
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
mf_rem_used(MemFile *mfp, BlockHeader *hp) {
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
mf_release(MemFile *mfp, int page_count) {

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
         ml_open_file(book);
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
   for (hp = mfp->usedLast; hp != NULL; hp = hp->bh_prev) {
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
//return TRUE if any memory was released
int
mf_release_all(void){
   Book* book;
   MemFile   *mfp;
   BlockHeader   *hp;
   int retval = FALSE;

   FOR_ALL_BOOKS(book) {
      mfp = book->mem.mfile;
      if (mfp) {
         // If no swap file yet, may open one
         if (mfp->fd < 0 && book->maySwap)
            ml_open_file(book);

         // only if there is a swapfile
         if (mfp->fd >= 0) {
            for (hp = mfp->usedLast; hp != NULL; ) {
               if (!(hp->bh_flags & BH_LOCKED)
                      && (!(hp->bh_flags & BH_DIRTY) || mf_write(mfp, hp) != FAIL)) {
                  mf_rem_used(mfp, hp);
                  mf_rem_hash(mfp, hp);
                  mf_free_bhdr(hp);
                  hp = mfp->usedLast;   // re-start, list was changed
                  retval = TRUE;
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
mf_alloc_bhdr(MemFile *mfp, int page_count) {
   BlockHeader   *hp;

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
mf_free_bhdr(BlockHeader *hp) {
   eeglFree(hp->bh_data);
   eeglFree(hp);
}

// Insert entry *hp in the free list.
private void
mf_ins_free(MemFile *mfp, BlockHeader *hp) {
   hp->bh_next = mfp->freeFirst;
   mfp->freeFirst = hp;
}

// remove the first entry from the free list and return a pointer to it
// Note: caller must check that mfp->freeFirst is not NULL!
private BlockHeader *
mf_rem_free(MemFile *mfp) {
   BlockHeader* hp = mfp->freeFirst;
   mfp->freeFirst = hp->bh_next;
   return hp;
}

// Read a block from disk. Return FAIL for failure, OK otherwise
private int
mf_read(MemFile *mfp, BlockHeader *hp) {
   FileSize   offset;
   unsigned   page_size;
   unsigned   size;

   if (mfp->fd < 0)       // there is no file, can't read
      return FAIL;

   page_size = mfp->pageSize;
   offset = (FileSize)page_size * hp->bh_bnum;
   size = page_size * hp->pageCount;
   if (lseek(mfp->fd, offset, SEEK_SET) != offset) {
      PERROR(_(e_seek_error_in_swap_file_read));
      return FAIL;
   }
   if ((unsigned)read_eintr(mfp->fd, hp->bh_data, size) != size) {
      PERROR(_(e_read_error_in_swap_file));
      return FAIL;
   }

   return OK;
}

// write a block to disk. Return FAIL for failure, OK otherwise
private int
mf_write(MemFile *mfp, BlockHeader *hp) {
   FileSize   offset;       // offset in the file
   BlockId   nr;       // block nr which is being written
   BlockHeader   *hp2;
   unsigned   page_size;  // number of bytes in a page
   unsigned   page_count; // number of pages written
   unsigned   size;       // number of bytes written

   if (mfp->fd < 0 && !mfp->mf_reopen)
      // there is no file and there was no file, can't write
      return FAIL;

   if (hp->bh_bnum < 0 && mf_trans_add(mfp, hp) == FAIL) // must assign file block number
      return FAIL;

   page_size = mfp->pageSize;

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

      offset = (FileSize)page_size * nr;
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
            did_swapwrite_msg = TRUE;
            return FAIL;
         }
      }

      did_swapwrite_msg = FALSE;
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
mf_write_block(
   MemFile   *mfp,
   BlockHeader   *hp,
   FileSize   offset UNUSED,
   unsigned   size)
{
   Byte   *data = hp->bh_data;
   int      result = OK;

   if ((unsigned)write_eintr(mfp->fd, data, size) != size)
      result = FAIL;

   if (data != hp->bh_data)
      eeglFree(data);

   return result;
}

// Make block number for *hp positive and add it to the translation list
// Return FAIL for failure, OK otherwise
private int
mf_trans_add(MemFile *mfp, BlockHeader *hp) {
   BlockHeader   *freep;
   BlockId   new_bnum;
   NR_TRANS   *np;
   int      page_count;

   if (hp->bh_bnum >= 0)          // it's already positive
      return OK;

   if ((np = ALLOC_ONE(NR_TRANS)) == NULL)
      return FAIL;

   //Get a new number for the block.
   //If the first item in the free list has sufficient pages, use its number
   //Otherwise use mf_blocknr_max.
   freep = mfp->freeFirst;
   page_count = hp->pageCount;
   if (freep != NULL && freep->pageCount >= page_count) {
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
BlockId
mf_trans_del(MemFile *mfp, BlockId old_nr) {
   NR_TRANS* np = (NR_TRANS *)mf_hash_find(&mfp->mf_trans, old_nr);

   if (np == NULL)      // not found
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
void
mf_set_ffname(MemFile *mfp) {
   mfp->fullFName = FullName_save(mfp->fName, FALSE);
}

// Make the name of the file used for the memfile a full path. Used before doing a :cd
void
mf_fullname(MemFile *mfp) {
   if (mfp == NULL || mfp->fName == NULL || mfp->fullFName == NULL)
      return;

   eeglFree(mfp->fName);
   mfp->fName = mfp->fullFName;
   mfp->fullFName = NULL;
}

// TRUE if there are any translations pending for 'mfp'
int
mf_need_trans(MemFile *mfp) {
   return (mfp->fName != NULL && mfp->mf_neg_count > 0);
}

// Open a swap file for a memfile.
// The "fname" must be in allocated memory, and is consumed (also when an error occurs).
private void
mf_do_open(
   MemFile   *mfp,
   Byte   *fname,
   int      flags)      // flags for open()
{
   FileStat   sb;

   mfp->fName = fname;

   // Get the full path name before the open fname cannot be NameBuff, because it must 
   // have been allocated.
   mf_set_ffname(mfp);

   // Extra security check: When creating a swap file it really shouldn't
   // exist yet.  If there is a symbolic link, this is most likely an attack.
   if ((flags & O_CREAT) && lstat((char *)mfp->fName, &sb) >= 0) {
      mfp->fd = -1;
      emsg(_(e_swap_file_already_exists_symlink_attack));
   } else {
      // try to open the file
      flags |= O_EXTRA | O_NOFOLLOW;
      mfp->mf_flags = flags;
      mfp->fd = openRw((char *)mfp->fName, flags);
   }

   // If the file cannot be opened, use memory only
   if (mfp->fd < 0) {
      EE_CLEAR(mfp->fName);
      EE_CLEAR(mfp->fullFName);
   } else {
      int fdflags = fcntl(mfp->fd, F_GETFD);
      if (fdflags >= 0 && (fdflags & FD_CLOEXEC) == 0)
          (void)fcntl(mfp->fd, F_SETFD, fdflags | FD_CLOEXEC);
#if defined(HAVE_SELINUX) || defined(HAVE_APPARMOR)
      mch_copy_sec(fname, mfp->fName);
#endif
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
mf_hash_init(MfHashTable *mht) {
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
mf_hash_free_all(MfHashTable *mht) {
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
mf_hash_add_item(MfHashTable *mht, MfHashItem *mhi) {
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
   buckets = lallocZeroed(size, FALSE);
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
//{{{garbage collection of variables

// When recursively copying lists and dicts we need to remember which ones we
// have done to avoid endless recursiveness.  This unique ID is used for that.
// The last bit is used for previous_funccal, ignored when comparing.
private int current_copyID = 0;

private int free_unref_items(int copyID);

// Return the next (unique) copy ID. Used for serializing nested structures.
int
get_copyID(void) {
   current_copyID += COPYID_INC;
   return current_copyID;
}

// Garbage collection for lists and dictionaries.
//
// We use reference counts to be able to free most items right away when they
// are no longer used.  But for composite items it's possible that it becomes
// unused while the reference count is > 0: When there is a recursive
// reference.  Example:
//   :let l = [1, 2, 3]
//   :let d = {9: l}
//   :let l[1] = d
//
// Since this is quite unusual we handle this with garbage collection: every
// once in a while find out which lists and dicts are not referenced from any
// variable.
//
// Here is a good reference text about garbage collection (refers to Python
// but it applies to all reference-counting mechanisms):
//   http://python.ca/nas/python/gc/

// Perform garbage collection for lists and dicts.
// When "testing" is TRUE this is called from test_garbagecollect_now().
// Return TRUE if some memory was freed.
int
garbage_collect(int testing) {
   int      copyID;
   int      abort = FALSE;
   Book   *book;
   Portal   *wp;
   int      did_free = FALSE;
   Tab   *tab;

   if (!testing) {
      // Only do this once.
      want_garbage_collect = FALSE;
      may_garbage_collect = FALSE;
      garbage_collect_at_exit = FALSE;
   }

   // The execution stack can grow big, limit the size.
   if (exestack.cap - exestack.len > 500) {
      Unt   new_len;
      Byte   *pp;

      // Keep 150% of the current size, with a minimum of the growth size.
      int n = exestack.len / 2;
      if (n < exestack.ga_growsize)
          n = exestack.ga_growsize;

      // Don't make it bigger though.
      if (exestack.len + n < exestack.cap) {
         new_len = (Unt)exestack.ga_itemsize * (exestack.len + n);
         pp = eeRealloc(exestack.c, new_len);
         exestack.cap = exestack.len + n;
         exestack.c = pp;
      }
   }

   // We advance by two because we add one for items referenced through previous_funccal.
   copyID = get_copyID();

   // * 1. Go through all accessible variables and mark all lists and dicts with copyID.

   // Don't free variables in the previous_funccal list unless they are only
   // referenced through previous_funccal.  This must be first, because if
   // the item is referenced elsewhere the funccal must not be freed.
   abort = abort || set_ref_in_previous_funccal(copyID);

   //script-local variables
   abort = abort || garbage_collect_scriptvars(copyID);

   //book-local variables
   FOR_ALL_BOOKS(book) {
      abort = abort || set_ref_in_item(&book->bookVar.c, copyID, NULL, NULL);
   } 

   // portal-local variables
   FOR_ALL_TAB_PORTALS(tab, wp)
      abort = abort || set_ref_in_item(&wp->wVar.c, copyID,  NULL, NULL);
   // portal-local variables in autocmd portals
   for (int i = 0; i < AUCMD_PORTAL_COUNT; ++i) {
      if (autoCommPortG[i].port) {
          abort = abort || set_ref_in_item( &autoCommPortG[i].port->wVar.c, copyID, NULL, NULL);
      } 
   } 
   FOR_ALL_POPUPPORTS(wp)
      abort = abort || set_ref_in_item(&wp->wVar.c, copyID, NULL, NULL);
   FOR_ALL_TABS(tab) {
      FOR_ALL_POPUPPORTS_IN_TAB(tab, wp)
         abort = abort || set_ref_in_item(&wp->wVar.c, copyID, NULL, NULL);
   } 

   // tab-local variables
   FOR_ALL_TABS(tab) {
      abort = abort || set_ref_in_item(&tab->tabVar.c, copyID, NULL, NULL);
   }
   // global variables
   abort = abort || garbage_collect_globvars(copyID)
          // function-local variables
          || set_ref_in_call_stack(copyID)
          // named functions (matters for closures)
          || set_ref_in_functions(copyID)
          // function call arguments, if v:testing is set.
          || set_ref_in_func_args(copyID)
          // loopvars keep variables for loop blocks
          || set_ref_in_loopvars(copyID);

    // v: vars
    abort = abort || garbageCollectEeglVars(copyID)
          // callbacks in books
          || setRefInBooks(copyID)
          // @completefunc, @omnifunc and @thesaurusfunc callbacks
          || set_ref_in_insexpand_funcs(copyID)
          // @operatorfunc callback
          || set_ref_in_opfunc(copyID)
          // @tagfunc callback
          || set_ref_in_tagfunc(copyID)
          // @findfunc callback
          || set_ref_in_findfunc(copyID);

    abort = abort || set_ref_in_channel(copyID)
          || set_ref_in_job(copyID)
          || set_ref_in_timer(copyID)
          || set_ref_in_quickfix(copyID)
          || set_ref_in_term(copyID)
          || set_ref_in_popups(copyID);

   if (!abort) {
      // 2. Free lists and dictionaries that are not referenced.
      did_free = free_unref_items(copyID);

      // 3. Check if any funccal can be freed now. This may call us back recursively.
      free_unref_funccal(copyID, testing);
   } ei (p_verbose > 0) {
      verb_msg(_("Not enough memory to set references, garbage collection aborted!"));
   }

   return did_free;
}

// Free lists, dictionaries, channels and jobs that are no longer referenced.
private int
free_unref_items(int copyID) {
   int      did_free = FALSE;

   // Let all "free" functions know that we are here.  This means no
   // dictionaries, lists, channels or jobs are to be freed, because we will do that here.
   in_free_unref_items = TRUE;

   // PASS 1: free the contents of the items.  We don't free the items
   // themselves yet, so that it is possible to decrement refcount counters

   // Go through the list of dicts and free items without this copyID.
   did_free |= dict_free_nonref(copyID);

   // Go through the list of lists and free items without this copyID.
   did_free |= list_free_nonref(copyID);

   // Go through the list of jobs and free items without the copyID. This
   // must happen before doing channels, because jobs refer to channels, but
   // the reference from the channel to the job isn't tracked.
   did_free |= free_unused_jobs_contents(copyID, COPYID_MASK);

   // Go through the list of channels and free items without the copyID.
   did_free |= free_unused_channels_contents(copyID, COPYID_MASK);

   // PASS 2: free the items themselves.
   dict_free_items(copyID);
   list_free_items(copyID);

   // Go through the list of jobs and free items without the copyID. This
   // must happen before doing channels, because jobs refer to channels, but
   // the reference from the channel to the job isn't tracked.
   free_unused_jobs(copyID, COPYID_MASK);

   // Go through the list of channels and free items without the copyID.
   free_unused_channels(copyID, COPYID_MASK);

   in_free_unref_items = FALSE;

   return did_free;
}

//Mark all lists and dicts referenced through EeSet "eeset" with "copyID".
//"list_stack" is used to add lists to be marked.  Can be NULL.
//
//Return TRUE if setting references failed somehow.
int
setRefInSet(EeSet* eeset, int copyID, ListStack   **list_stack) {
   int      todo;
   int      abort = FALSE;
   EeSetItem* hi;
   EeSet   *cur_ht;
   HtStack   *ht_stack = NULL;
   HtStack   *tempitem;

   cur_ht = eeset;
   for (;;) {
      if (!abort) {
         // Mark each item in the hashtab.  If the item contains a hashtab
         // it is added to ht_stack, if it contains a list it is added to list_stack.
         todo = (int)cur_ht->count;
         FOR_ALL_HASHTAB_ITEMS(cur_ht, hi, todo) {
            if (!HASHITEM_EMPTY(hi)) {
               --todo;
               abort = abort || set_ref_in_item(&HI2DI(hi)->c, copyID, &ht_stack, list_stack);
            }
         }
      }

      if (ht_stack == NULL)
         break;

      // take an item from the stack
      cur_ht = ht_stack->ht;
      tempitem = ht_stack;
      ht_stack = ht_stack->prev;
      free(tempitem);
   }

   return abort;
}

#if defined(PROTO)

// Mark a dict and its items with "copyID". Return TRUE if setting references failed somehow.
int
set_ref_in_dict(Bag* b, int copyID) {
   if (b && b->copyId != copyID) {
      b->copyId = copyID;
      return setRefInSet(&b->hashTable, copyID, NULL, NULL);
   }
   return FALSE;
}
#endif

// Mark a list and its items with "copyID". Return TRUE if setting references failed somehow.
int
set_ref_in_list(List *ll, int copyID) {
   if (ll && ll->copyId != copyID) {
      ll->copyId = copyID;
      return set_ref_in_list_items(ll, copyID, NULL);
   }
   return FALSE;
}

//Mark all lists and dicts referenced through list "l" with "copyID".
//"ht_stack" is used to add hashtabs to be marked.  Can be NULL.
//
//Return TRUE if setting references failed somehow.
int
set_ref_in_list_items(List      *l, int copyID, HtStack** ht_stack) {
   ListItem    *li;
   int       abort = FALSE;
   List    *cur_l;
   ListStack *list_stack = NULL;
   ListStack *tempitem;

   cur_l = l;
   for (;;) {
      if (!abort && cur_l->first != &range_list_item)
         // Mark each item in the list.  If the item contains a hashtab
         // it is added to ht_stack, if it contains a list it is added to list_stack.
         for (li = cur_l->first; !abort && li != NULL; li = li->next)
            abort = abort || set_ref_in_item(&li->c, copyID, ht_stack, &list_stack);
      if (list_stack == NULL)
         break;

      // take an item from the stack
      cur_l = list_stack->list;
      tempitem = list_stack;
      list_stack = list_stack->prev;
      free(tempitem);
   }

   return abort;
}

// Mark the partial in callback 'cb' with "copyID".
int
memSetRefInCallback(Callback *cb, int copyID) {
   if (cb->name == NULL || *cb->name == ZERO || cb->cb_partial == NULL)
      return FALSE;

   Var tv;
   tv.tag = VAR_PARTIAL;
   tv.partial = cb->cb_partial;
   return set_ref_in_item(&tv, copyID, NULL, NULL);
}

// Mark the dict "dd" with "copyID". Also see set_ref_in_item().
private int
set_ref_in_item_dict(
   Bag* bag,
   int         copyID,
   HtStack      **ht_stack,
   ListStack   **list_stack
){
   if (!bag || bag->copyId == copyID)
      return FALSE;

   // Didn't see this bag yet.
   bag->copyId = copyID;
   if (!ht_stack)
      return setRefInSet(&bag->hashTable, copyID, list_stack);

   HtStack *newitem = ALLOC_ONE(HtStack);
   newitem->ht = &bag->hashTable;
   newitem->prev = *ht_stack;
   *ht_stack = newitem;

   return FALSE;
}

// Mark the list "ll" with "copyID". Also see set_ref_in_item().
private int
set_ref_in_item_list(
   List      *ll,
   int         copyID,
   HtStack      **ht_stack,
   ListStack   **list_stack
) {
   if (!ll || ll->copyId == copyID)
      return FALSE;

   // Didn't see this list yet.
   ll->copyId = copyID;
   if (list_stack == NULL)
      return set_ref_in_list_items(ll, copyID, ht_stack);

   ListStack *newitem = ALLOC_ONE(ListStack);
   if (newitem == NULL)
      return TRUE;

   newitem->list = ll;
   newitem->prev = *list_stack;
   *list_stack = newitem;

   return FALSE;
}

// Mark the partial "pt" with "copyID". Also see set_ref_in_item().
private int
set_ref_in_item_partial(
   PartiallyApplied* pt,
   int copyID,
   HtStack** ht_stack,
   ListStack** list_stack
) {
   if (!pt)
      return FALSE;

   int abort = set_ref_in_func(pt->name, pt->fn, copyID);

   if (pt->self != NULL) {
      Var dtv;
      dtv.tag = VAR_BAG;
      dtv.bag = pt->self;
      set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
   }


   for (int i = 0; i < pt->argc; ++i)
      abort = abort || set_ref_in_item(&pt->argv[i], copyID,
         ht_stack, list_stack);
   // pt_loopvars is handled in set_ref_in_loopvars()

   return abort;
}

// Mark the job "pt" with "copyID". Also see set_ref_in_item().
private int
set_ref_in_item_job(
   Job* job,
   int         copyID,
   HtStack      **ht_stack,
   ListStack   **list_stack
) {
   Var    dtv;

   if (job == NULL || job->jv_copyID == copyID)
      return FALSE;

   job->jv_copyID = copyID;
   if (job->jv_channel != NULL) {
      dtv.tag = VAR_CHANNEL;
      dtv.channel = job->jv_channel;
      set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
   }
   if (job->jv_exit_cb.cb_partial != NULL) {
      dtv.tag = VAR_PARTIAL;
      dtv.partial = job->jv_exit_cb.cb_partial;
      set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
   }

   return FALSE;
}

// Mark the channel "ch" with "copyID". Also see set_ref_in_item().
private int
set_ref_in_item_channel(
   Channel      *ch,
   int         copyID,
   HtStack      **ht_stack,
   ListStack   **list_stack
) {
   Var    dtv;

   if (ch == NULL || ch->copyId == copyID)
      return FALSE;

   ch->copyId = copyID;
   for (ChannelFdKind part = PART_SOCK; part < PART_COUNT; ++part) {
      for (JsonQ *jq = ch->fds[part].ch_json_head.jq_next; jq; jq = jq->jq_next)
         set_ref_in_item(jq->jq_value, copyID, ht_stack, list_stack);
      for (CbNode *cq = ch->fds[part].ch_cb_head.cq_next; cq != NULL; cq = cq->cq_next)
         if (cq->cq_callback.cb_partial != NULL) {
            dtv.tag = VAR_PARTIAL;
            dtv.partial = cq->cq_callback.cb_partial;
            set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
         }
      if (ch->fds[part].ch_callback.cb_partial != NULL) {
         dtv.tag = VAR_PARTIAL;
         dtv.partial = ch->fds[part].ch_callback.cb_partial;
         set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
      }
   }
   if (ch->ch_callback.cb_partial != NULL) {
      dtv.tag = VAR_PARTIAL;
      dtv.partial = ch->ch_callback.cb_partial;
      set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
   }
   if (ch->ch_close_cb.cb_partial != NULL) {
      dtv.tag = VAR_PARTIAL;
      dtv.partial = ch->ch_close_cb.cb_partial;
      set_ref_in_item(&dtv, copyID, ht_stack, list_stack);
   }

   return FALSE;
}

// Mark all lists, dicts and other container types referenced through Var "tv" with "copyID".
// "list_stack" is used to add lists to be marked. May be NULL.
// "ht_stack" is used to add hashtabs to be marked. May be NULL.
//
// Return TRUE if setting references failed somehow.
int
set_ref_in_item(
   Var       *tv,
   int          copyID,
   HtStack       **ht_stack,
   ListStack    **list_stack
){
   int abort = FALSE;

   switch (tv->tag) {
   case VAR_BAG:
      return set_ref_in_item_dict(tv->bag, copyID, ht_stack, list_stack);
   case VAR_LIST: return set_ref_in_item_list(tv->list, copyID, ht_stack, list_stack);
   case VAR_FUNC: abort = set_ref_in_func(tv->string, NULL, copyID); break;
   case VAR_PARTIAL:
       return set_ref_in_item_partial(tv->partial, copyID, ht_stack, list_stack);

   case VAR_JOB:
       return set_ref_in_item_job(tv->job, copyID, ht_stack, list_stack);

   case VAR_CHANNEL:
       return set_ref_in_item_channel(tv->channel, copyID, ht_stack, list_stack);

   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_BOOL:
   case VAR_SPECIAL:
   case VAR_NUMBER:
   case VAR_FLOAT:
   case VAR_STRING:
   case VAR_BLOB:
       // Types that do not contain any other item
       break;
   }

   return abort;
}

//}}}
