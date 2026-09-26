typedef enum {
   LL_ACTION_INVALID, //placeholder for ill-defined strings
   LL_ACTION_ADD, //add entry to location list
   LL_ACTION_REPLACE, 
   LL_ACTION_UPDATE,
   LL_ACTION_NEW, //create new location list
   LL_ACTION_FREE
} LocListAction;
#define SIGN_DEF_PRIO   10
#define FM_BACKWARD  0x01   //search backwards
#define FM_FORWARD   0x02   //search forwards
#define FM_BLOCKSTOP 0x04   //stop at start/end of block
#define FM_SKIPCOMM  0x08   //skip comments
#define SEARCH_STAT_DEF_TIMEOUT 40L
