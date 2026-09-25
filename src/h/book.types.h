typedef struct mf_hashtab_S {
   Ulong mask; // mask used for hash value (nr of items in array is "mht_mask" + 1)
   Ulong mht_count;       // nr of items inserted into hashtable
   MfHashItem** mht_buckets;  // points to mht_small_buckets or dynamically allocated array
   MfHashItem* mht_small_buckets[MHT_INIT_SIZE];   // initial buckets
   Byte mht_fixed;       // non-zero value forbids growth
} MfHashTable;
typedef enum {
   MF_DIRTY_NO = 0,      // no dirty blocks
   MF_DIRTY_YES,      // there are dirty blocks
   MF_DIRTY_YES_NOSYNC,   // there are dirty blocks, do not sync yet
} MfDirty;
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
