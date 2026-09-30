#define LOC_LIST_MAKE      0 //selectable with ":list m"
#define LOC_LIST_GREP      1 //selectable with ":list g"
#define LOC_LIST_HELP      2 //selectable with ":list h"
#define LOC_LIST_TAGS      3 //selectable with ":list t"
#define LOC_LIST_BOOKMARKS 4 //selectable with ":list b"
#define LOC_LIST_CSCOPE    5 //selectable with ":list c"
#define COUNT_LOC_LISTS    6 //= 1 + highest LIST_...value
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
