typedef enum {
   LL_ACTION_INVALID, // placeholder for ill-defined strings
   LL_ACTION_ADD, // add entry to location list
   LL_ACTION_REPLACE, 
   LL_ACTION_UPDATE,
   LL_ACTION_NEW, // create new location list
   LL_ACTION_FREE
} LocListAction;
#define SIGN_DEF_PRIO   10
