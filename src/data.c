//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## data.c: core data structures

#include "eegl.h"

//{{{forward declarations

private int check_for_string_or_list_or_blob_arg(Arr(Var) args, int idx);
private void dict_free(Bag *d);
private void string_reduce(Var* argvars, Var* expr, Var* returnVar);

//}}}
//{{{list

// List heads for garbage collection.
private List      *first_list = NULL;   // list of all lists

#define FOR_ALL_WATCHERS(l, lw) \
    for ((lw) = (l)->watcher; (lw) != NULL; (lw) = (lw)->next)

private void list_free_item(List *l, ListItem *item);

// Add a watcher to a list.
void
list_add_watch(List *l, ListWatch *lw) {
   lw->next = l->watcher;
   l->watcher = lw;
}

// Remove a watcher from a list. No warning when it isn't found...
void
list_rem_watch(List* l, ListWatch* lwrem) {
   ListWatch* lw;
   ListWatch** lwp = &l->watcher;
   FOR_ALL_WATCHERS(l, lw) {
      if (lw == lwrem) {
          *lwp = lw->next;
          break;
      }
      lwp = &lw->next;
   }
}

// Just before removing an item from a list: advance watchers to the next item.
private void
list_fix_watch(List* l, ListItem* item) {
   ListWatch   *lw;
   FOR_ALL_WATCHERS(l, lw) {
      if (lw->c == item)
          lw->c = item->next;
   } 
}


// Prepend the list to the list of lists for garbage collection.
private void
registerForGc(List* l) {
   if (first_list)
      first_list->usedPrev = l;
   l->usedPrev = NULL;
   l->usedNext = first_list;
   first_list = l;
}

// Allocate an empty header for a list. Caller should take care of the reference count.
List *
list_alloc(void) {
   List* l = ALLOC_CLEAR_ONE(List);
   registerForGc(l);
   return l;
}

// list_alloc() with an ID for alloc_fail().
List *
list_alloc_id(AllocId id UNUSED) {
   if (alloc_fail_id == id && alloc_does_fail(sizeof(List)))
      return NULL;
   return (list_alloc());
}

// Allocate space for a list, plus "count" items. This uses one allocation for efficiency.
// The reference count is not set. Next list_set_item() must be called for each item.
List *
list_alloc_with_items(int count) {
   List* l = (List *)allocZeroed(sizeof(List) + count * sizeof(ListItem));

   registerForGc(l);

   if (count <= 0)
      return l;

   ListItem   *li = (ListItem *)(l + 1);
   int      i;

   l->len = count;
   l->withItems = count;
   l->first = li;
   l->lv_u.mat.last = li + count - 1;
   for (i = 0; i < count; ++i) {
      if (i == 0)
         li->prev = NULL;
      else
         li->prev = li - 1;
      if (i == count - 1)
         li->next = NULL;
      else
         li->next = li + 1;
      ++li;
   }

   return l;
}

// Set item "idx" for a list previously allocated with list_alloc_with_items().
// The contents of "tv" is moved into the list item. Each item must be set exactly once.
void
list_set_item(List *l, int idx, Var *tv) {
   ListItem* li = (ListItem *)(l + 1) + idx;
   li->c = *tv;
}

// Allocate an empty list for a return value, with reference count set. Return OK or FAIL.
void
allocReturnList(OUT Var* returnVar) {
   returnVar->lock = 0;
   returnVar_list_set(OUT returnVar, list_alloc());
}

// Allocate an empty list for a return value, with reference count set. Return OK or FAIL.
// Uses an allocation id for testing.
int
allocReturnList_id(Var* returnVar, AllocId id) {
   if (id == alloc_fail_id && alloc_does_fail(sizeof(List)))
      return FAIL;
   allocReturnList(returnVar);
   return OK;
}


// Set a list as the return value.  Increments the reference count.
void
returnVar_list_set(OUT Var* returnVar, List *l) {
   returnVar->tag = VAR_LIST;
   returnVar->list = l;
   if (l)
      ++l->refcount;
}

// Unreference a list: decrement the reference count and free it when it becomes zero.
void
list_unref(List* l) {
   if (l && --l->refcount <= 0)
      list_free(l);
}

// Free a list, including all non-container items it points to. Ignores the reference count.
private void
list_free_contents(List* l) {
   ListItem *item;

   if (l->first != &range_list_item) {
      for (item = l->first; item; item = l->first) {
         // Remove the item before deleting it.
         l->first = item->next;
         clearVar(&item->c);
         list_free_item(l, item);
      }
   } 
}

// Go through the list of lists and free items without the copyID. But don't free a list that has 
// a watcher (used in a for loop), these are not referenced anywhere.
int
list_free_nonref(int copyID) {
   int did_free = false;

   for (List* ll = first_list; ll != NULL; ll = ll->usedNext) {
      if ((ll->copyId & COPYID_MASK) != (copyID & COPYID_MASK) && ll->watcher == NULL) {
          // Free the List and ordinary items it contains, but don't recurse
          // into Lists and Dictionaries, they will be in the list of dicts or list of lists.
          list_free_contents(ll);
          did_free = true;
      }
   } 
   return did_free;
}

private void
list_free_list(List* l) {
   // Remove the list from the list of lists for garbage collection.
   if (!l->usedPrev)
      first_list = l->usedNext;
   else
      l->usedPrev->usedNext = l->usedNext;
   if (l->usedNext)
      l->usedNext->usedPrev = l->usedPrev;

   free_type(l->ty);
   eeglFree(l);
}

void
list_free_items(int copyID) {
   List* ll_next;
   for (List* ll = first_list; ll != NULL; ll = ll_next) {
      ll_next = ll->usedNext;
      if ((ll->copyId & COPYID_MASK) != (copyID & COPYID_MASK) && ll->watcher == NULL) {
         // Free the List and ordinary items it contains, but don't recurse
         // into Lists and Dictionaries, they will be in the list of dicts or list of lists.
         list_free_list(ll);
      }
   }
}

void
list_free(List* l) {
   if (in_free_unref_items)
      return;

   list_free_contents(l);
   list_free_list(l);
}

// Allocate a list item. It is not initialized, don't forget to set lock.
ListItem *
listitem_alloc(void) {
   return ALLOC_ONE(ListItem);
}

// Free a list item, unless it was allocated together with the list itself.
// Does not clear the value.  Does not notify watchers.
private void
list_free_item(List *l, ListItem *item) {
   if (l->withItems == 0 || item < (ListItem *)l
            || item >= (ListItem *)(l + 1) + l->withItems)
      eeglFree(item);
}

// Free a list item, unless it was allocated together with the list itself.
// Also clears the value.  Does not notify watchers.
void
listitem_free(List* l, ListItem* item) {
    clearVar(&item->c);
    list_free_item(l, item);
}

// Remove a list item from a List and free it.  Also clears the value.
void
listitem_remove(List* l, ListItem* item) {
    list_remove(l, item, item);
    listitem_free(l, item);
}

// Get the number of items in a list.
long
list_len(List* l) {
   if (!l)
      return 0L;
   return l->len;
}

// Return true when two lists have exactly the same values.
int
list_equal(List* l1, List* l2, int ic) {  // ignore case for strings

   if (l1 == l2)
      return true;
   if (list_len(l1) != list_len(l2))
      return false;
   if (list_len(l1) == 0)
      // empty and NULL list are considered equal
      return true;
   if (l1 == NULL || l2 == NULL)
      return false;

   CHECK_LIST_MATERIALIZE(l1);
   CHECK_LIST_MATERIALIZE(l2);

   ListItem* item1;
   ListItem* item2;
   for (item1 = l1->first, item2 = l2->first;
        item1 != NULL && item2 != NULL;
        item1 = item1->next, item2 = item2->next
   ) {
      if (!tv_equal(&item1->c, &item2->c, ic))
         return false;
   } 
   return item1 == NULL && item2 == NULL;
}

// Locate item with index "n" in list "l" and return it. A negative index is counted from the end;
// -1 is the last item. Return NULL when "n" is out of range.
ListItem *
list_find(List* l, long n) {
   ListItem* item;
   long   idx;

   if (!l)
      return NULL;

   // Negative index is relative to the end.
   if (n < 0)
      n = l->len + n;

   // Check for index out of range.
   if (n < 0 || n >= l->len)
      return NULL;

   CHECK_LIST_MATERIALIZE(l);

   // range_list_materialize may reset l->len
   if (n >= l->len)
      return NULL;

   // When there is a cached index may start search from there.
   if (l->lv_u.mat.cachedItem != NULL) {
      if (n < l->lv_u.mat.cachedInd / 2) {
         // closest to the start of the list
         item = l->first;
         idx = 0;
      } ei (n > (l->lv_u.mat.cachedInd + l->len) / 2) {
         // closest to the end of the list
         item = l->lv_u.mat.last;
         idx = l->len - 1;
      } else {
         // closest to the cached index
         item = l->lv_u.mat.cachedItem;
         idx = l->lv_u.mat.cachedInd;
      }
   } else {
      if (n < l->len / 2) {
         // closest to the start of the list
         item = l->first;
         idx = 0;
      } else {
         // closest to the end of the list
         item = l->lv_u.mat.last;
         idx = l->len - 1;
      }
   }

   while (n > idx) {
      // search forward
      item = item->next;
      ++idx;
   }
   while (n < idx) {
      // search backward
      item = item->prev;
      --idx;
   }

   // cache the used index
   l->lv_u.mat.cachedInd = idx;
   l->lv_u.mat.cachedItem = item;

   return item;
}

// Get list item "l[idx]" as a number.
long
list_find_nr(List* l, long idx, OUT Boole* errorp) {  // set to true when something wrong
   if (l && l->first == &range_list_item) {
      long n = idx;

      // not materialized range() list: compute the value. Negative index is relative to the end.
      if (n < 0)
         n = l->len + n;

      // Check for index out of range.
      if (n < 0 || n >= l->len) {
         if (errorp)
            *errorp = true;
         return -1L;
      }

      return l->lv_u.nonmat.start + n * l->lv_u.nonmat.stride;
   }

   ListItem* li = list_find(l, idx);
   if (!li) {
      if (errorp)
         *errorp = true;
      return -1L;
   }
   return (long)varGetNumberChk(&li->c, OUT errorp);
}

// Get list item "l[idx - 1]" as a string.  Returns NULL for failure.
CS
list_find_str(List *l, long idx) {
   ListItem* li = list_find(l, idx - 1);
   if (!li) {
      showErrFmtMsg(_(e_list_index_out_of_range_nr), idx);
      return null;
   }
   return tv_get_string(&li->c);
}

// Like list_find() but when a negative index is used that is not found use
// zero and set "idx" to zero.  Used for first index of a range.
ListItem *
list_find_index(List *l, long *idx) {
   ListItem *li = list_find(l, *idx);

   if (li)
      return li;

   if (*idx < 0) {
      *idx = 0;
      li = list_find(l, *idx);
   }
   return li;
}

// Locate "item" list "l" and return its index. Returns -1 when "item" is not in the list.
long
list_idx_of_item(List *l, ListItem *item) {
   if (!l)
      return -1;
   
   CHECK_LIST_MATERIALIZE(l);
   long idx = 0;
   ListItem   *li;
   for (li = l->first; li != NULL && li != item; li = li->next)
      ++idx;
   if (li == NULL)
      return -1;
   return idx;
}

// Append item "item" to the end of list "l".
void
list_append(List* l, ListItem* item) {
   CHECK_LIST_MATERIALIZE(l);
   if (l->lv_u.mat.last == NULL) {
      // empty list
      l->first = item;
      item->prev = NULL;
   } else {
      l->lv_u.mat.last->next = item;
      item->prev = l->lv_u.mat.last;
   }
   l->lv_u.mat.last = item;
   ++l->len;
   item->next = NULL;
}

// Append Var "tv" to the end of list "l". "tv" is copied. 
// Return FAIL when out of memory or the tag is wrong.
int
list_append_tv(List* l, Var* newVal) {
   ListItem* newItem = listitem_alloc();
      
   copy_tv(OUT &newItem->c, newVal);
   list_append(l, newItem);
   return OK;
}

// Append Var "tv" to the end of list "l". "tv" is moved. 
// Return FAIL when out of memory or the tag is wrong.
private int
list_append_tv_move(List* l, Var* tv) {
   ListItem* li = listitem_alloc();
      
   li->c = *tv;
   list_append(l, li);
   return OK;
}

// Add a dictionary to a list.  Used by getqflist(). Return FAIL when out of memory.
int
listAppendBag(List *list, Bag* bag) {
   ListItem   *li = listitem_alloc();

   li->c = (Var){.tag = VAR_BAG, .lock = 0, .bag = bag};
   list_append(list, li);
   ++bag->refcount;
   return OK;
}

// Append list2 to list1. Return FAIL when out of memory.
int
list_append_list(List *list1, List *list2) {
   ListItem   *li = listitem_alloc();
      
   li->c = (Var){.tag = VAR_LIST, .lock = 0, .list = list2};
   list_append(list1, li);
   ++list2->refcount;
   return OK;
}

// Make a copy of "str" and append it as an item to list "l".
// When "len" >= 0 use "str[len]". Returns FAIL when out of memory.
int
list_append_string(List *l, CS str, int len) {
   ListItem *li = listitem_alloc();
      
   list_append(l, li);
   li->c.tag = VAR_STRING;
   li->c.lock = 0;
   if (!str)
      li->c.string = NULL;
   ei ((li->c.string = (len >= 0 ? copySubstr(str, len) : copyStr(str))) == NULL)
      return FAIL;
   return OK;
}


// Append "n" to list "l". Return FAIL when out of memory.
int
list_append_number(List* l, Long n) {
   ListItem* li = listitem_alloc();
   li->c = (Var){.tag = VAR_NUMBER, .lock = 0, .number = n};   
   list_append(l, li);
   return OK;
}

// Insert Var "tv" in list "l" before "item". If "item" is NULL, append at the end.
// Return FAIL when out of memory or the type is wrong.
int
list_insert_tv(List *l, Var *tv, ListItem *item) {
   ListItem* ni = listitem_alloc();
   copy_tv(OUT &ni->c, tv);
   list_insert(l, ni, item);
   return OK;
}

void
list_insert(List *l, ListItem *ni, ListItem *item) {
   CHECK_LIST_MATERIALIZE(l);
   if (!item)
      // Append new item at end of list.
      list_append(l, ni);
   else {
      // Insert new item before existing item.
      ni->prev = item->prev;
      ni->next = item;
      if (item->prev == NULL) {
         l->first = ni;
         ++l->lv_u.mat.cachedInd;
      } else {
         item->prev->next = ni;
         l->lv_u.mat.cachedItem = NULL;
      }
      item->prev = ni;
      ++l->len;
   }
}

// Get the list item in "l" with index "n1".  "n1" is adjusted if needed.
// Return NULL if there is no such item.
ListItem *
check_range_index_one(List* l, long* n1, int quiet) {
   long orig_n1 = *n1;
   ListItem* li = list_find_index(l, n1);

   if (li)
      return li;

   if (!quiet)
      showErrFmtMsg(_(e_list_index_out_of_range_nr), orig_n1);
   return NULL;
}

// Check that "n2" can be used as the second index in a range of list "l".
// If "n1" or "n2" is negative it is changed to the positive index.
// "li1" is the item for item "n1". Return OK or FAIL.
int
check_range_index_two(List* l, long* n1, ListItem* li1, long* n2, int quiet) {
   if (*n2 < 0) {
      ListItem   *ni = list_find(l, *n2);

      if (ni == NULL) {
         if (!quiet)
            showErrFmtMsg(_(e_list_index_out_of_range_nr), *n2);
         return FAIL;
      }
      *n2 = list_idx_of_item(l, ni);
   }

   // Check that n2 isn't before n1.
   if (*n1 < 0)
      *n1 = list_idx_of_item(l, li1);
   if (*n2 < *n1) {
      if (!quiet)
         showErrFmtMsg(_(e_list_index_out_of_range_nr), *n2);
      return FAIL;
   }
    return OK;
}

// Assign values from list "src" into a range of "dest".
// "idx1_arg" is the index of the first item in "dest" to be replaced.
// "idx2" is the index of last item to be replaced, but when "empty_idx2" is
// true then replace all items after "idx1".
// "op" is the operator, normally "=" but can be "+=" and the like.
// "varname" is used for error messages. Return OK or FAIL.
int
list_assign_range(
   List* dest,
   List* src,
   long idx1_arg,
   long idx2,
   int empty_idx2,
   CS op,
   Text varname
) {
   long   idx1 = idx1_arg;
   ListItem   *first_li = list_find_index(dest, &idx1);
   long   idx;

   // Check whether any of the list items is locked before making any changes.
   idx = idx1;
   ListItem* destItem = first_li;
   for (ListItem* srcItem = src->first; srcItem != NULL && destItem != NULL; ) {
      if (value_check_lock(destItem->c.lock, varname, false))
         return FAIL;
      srcItem = srcItem->next;
      if (srcItem == NULL || (!empty_idx2 && idx2 == idx))
         break;
      destItem = destItem->next;
      ++idx;
   }

   // Assign the List values to the list items.
   idx = idx1;
   ListItem* srcItem;
   destItem = first_li;
   for (srcItem = src->first; srcItem; ) {
      if (op != NULL && *op != '=')
         tv_op(&destItem->c, &srcItem->c, op);
      else {
         clearVar(&destItem->c);
         copy_tv(OUT &destItem->c, &srcItem->c);
      }
      srcItem = srcItem->next;
      if (srcItem == NULL || (!empty_idx2 && idx2 == idx))
         break;
      if (destItem->next == NULL) {
         // Need to add an empty item.
         if (list_append_number(dest, 0) == FAIL) {
            srcItem = NULL;
            break;
         }
      }
      destItem = destItem->next;
      ++idx;
   }
   if (srcItem != NULL) {
      emsg(_(e_list_value_has_more_items_than_targets));
      return FAIL;
   }
   if (empty_idx2
          ? (destItem != NULL && destItem->next != NULL)
          : idx != idx2) {
      emsg(_(e_list_value_does_not_have_enough_items));
      return FAIL;
   }
   return OK;
}

// Flatten up to "maxitems" in "list", starting at "first" to depth "maxdepth".
// When "first" is NULL use the first item.
// It does nothing if "maxdepth" is 0. Return FAIL when out of memory.
private void
list_flatten(List* list, ListItem* first, long maxitems, long maxdepth) {
   int done = 0;

   if (maxdepth == 0)
      return;
   CHECK_LIST_MATERIALIZE(list);
   ListItem* item = first ? first : list->first; 

   while (item != NULL && done < maxitems) {
      ListItem   *next = item->next;

      fast_breakcheck();
      if (gotInterruptG)
          return;

      if (item->c.tag == VAR_LIST) {
         List   *itemlist = item->c.list;

         list_remove(list, item, item);
         if (list_extend(list, itemlist, next) == FAIL) {
            list_free_item(list, item);
            return;
         }

         if (maxdepth > 0) {
            list_flatten(list, item->prev == NULL
                       ? list->first : item->prev->next,
                  itemlist->len, maxdepth - 1);
         } 
         clearVar(&item->c);
         list_free_item(list, item);
      }

      ++done;
      item = next;
   }
}

// "flatten()" and "flattennew()" functions
private void
flatten_common(Arr(Var) argvars, Var* returnVar, int make_copy) {
   List  *l;
   long    maxdepth;
   Boole error = false;

   if (argvars[0].tag != VAR_LIST) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list), "flatten()");
      return;
   }

   if (argvars[1].tag == VAR_UNKNOWN)
      maxdepth = 999999;
   else {
      maxdepth = (long)varGetNumberChk(&argvars[1], OUT &error);
      if (error)
          return;
      if (maxdepth < 0) {
          emsg(_(e_maxdepth_must_be_non_negative_number));
          return;
      }
   }

   l = argvars[0].list;
   returnVar->tag = VAR_LIST;
   returnVar->list = l;
   if (l == NULL)
      return;

   if (make_copy) {
      l = list_copy(l, false, true, get_copyID());
      returnVar->list = l;
      if (l == NULL)
         return;
      // The type will change.
      free_type(l->ty);
      l->ty = NULL;
   } else {
      if (value_check_lock(l->lock, text(N_("flatten() argument")), true))
         return;
      ++l->refcount;
   }

    list_flatten(l, NULL, l->len, maxdepth);
}

// "flatten(list[, {maxdepth}])" function
void
f_flatten(Arr(Var) argvars, Var* returnVar) {
   flatten_common(argvars, returnVar, false);
}

// "flattennew(list[, {maxdepth}])" function
void
f_flattennew(Arr(Var) argvars, Var* returnVar) {
   flatten_common(argvars, returnVar, true);
}

// "items(list)" function Caller must have already checked that argvars[0] is a List.
void
list2items(Arr(Var) argvars, Var* returnVar) {
   List* l = argvars[0].list;

   allocReturnList(returnVar);
   if (!l)
      return;  // null list behaves like an empty list

   // TODO: would be more efficient to not materialize the argument
   CHECK_LIST_MATERIALIZE(l);
   ListItem* li;
   Long idx;
   for (idx = 0, li = l->first; li; li = li->next, ++idx) {
      List* l2 = list_alloc();

      if (list_append_list(returnVar->list, l2) == FAIL) {
         eeglFree(l2);
         break;
      }
      if (list_append_number(l2, idx) == FAIL || list_append_tv(l2, &li->c) == FAIL)
         break;
   }
}

// "items(string)" function. Precond: Caller must have already checked that argvars[0] is a String.
void
string2items(Arr(Var) argvars, Var* returnVar) {
   CS p = argvars[0].string;
   allocReturnList(returnVar);
   if (!p)  // null string behaves like an empty string
      return;

   for (Long idx = 0; *p != ZERO; ++idx) {
      int len = utfCharLen(p);
      if (len == 0)
         break;
      List* l2 = list_alloc();
      if (list_append_list(returnVar->list, l2) == FAIL) {
         eeglFree(l2);
         break;
      }
      if (list_append_number(l2, idx) == FAIL || list_append_string(l2, p, len) == FAIL)
         break;
      p += len;
   }
}

// Extend "l1" with "l2".  "l1" must not be NULL.
// If "bef" is NULL append at the end, otherwise insert before this item.
// Return FAIL when out of memory.
int
list_extend(List *l1, List *l2, ListItem *bef) {
   ListItem   *item;
   int      todo;
   ListItem   *bef_prev;

   // NULL list is equivalent to an empty list: nothing to do.
   if (l2 == NULL || l2->len == 0)
      return OK;

   todo = l2->len;
   CHECK_LIST_MATERIALIZE(l1);
   CHECK_LIST_MATERIALIZE(l2);

   // When exending a list with itself, at some point we run into the item
   // that was before "bef" and need to skip over the already inserted items
   // to "bef".
   bef_prev = bef == NULL ? NULL : bef->prev;

   // We also quit the loop when we have inserted the original item count of
   // the list, avoid a hang when we extend a list with itself.
   for (item = l2->first; item != NULL && --todo >= 0;
      item = item == bef_prev ? bef : item->next) {
      if (list_insert_tv(l1, &item->c, bef) == FAIL)
         return FAIL;
   } 
   return OK;
}

// Concatenate lists "l1" and "l2" into a new list, stored in "tv". Return FAIL when out of memory.
int
list_concat(List *l1, List *l2, Var *tv) {
   // make a copy of the first list.
   List* l = l1 ? list_copy(l1, false, true, 0) : list_alloc();
   tv->tag = VAR_LIST;
   tv->lock = 0;
   tv->list = l;
   if (!l1)
      ++l->refcount;

   // append all items from the second list
   return list_extend(l, l2, NULL);
}

List *
list_slice(List *ol, long n1, long n2) {
   List   *l = list_alloc();
   for (ListItem* item = list_find(ol, n1); n1 <= n2; ++n1) {
      if (list_append_tv(l, &item->c) == FAIL) {
         list_free(l);
         return NULL;
      }
      item = item->next;
   }
   return l;
}

int
list_slice_or_index(
   List* list,
   int range,
   Long n1_arg,
   Long n2_arg,
   int exclusive,
   Var* returnVar,
   int verbose
) {
   long   len = list_len(list);
   Long   n1 = n1_arg;
   Long   n2 = n2_arg;
   Var   var1;

   if (n1 < 0)
      n1 = len + n1;
   if (n1 < 0 || n1 >= len) {
      // For a range we allow invalid values and for legacy script return an empty list.
      // A list index out of range is an error.
      if (!range) {
         if (verbose)
            showErrFmtMsg(_(e_list_index_out_of_range_nr), (long)n1_arg);
         return FAIL;
      }
      n1 = len;
   }
   if (range) {
      List   *l;

      if (n2 < 0)
         n2 = len + n2;
      ei (n2 >= len)
         n2 = len - (exclusive ? 0 : 1);
      if (exclusive)
         --n2;
      if (n2 < 0 || n2 + 1 < n1)
         n2 = -1;
      l = list_slice(list, n1, n2);
      if (l == NULL)
         return FAIL;
      clearVar(returnVar);
      returnVar_list_set(returnVar, l);
   } else {
      // copy the item to "var1" to avoid that freeing the list makes it invalid.
      copy_tv(OUT &var1, &list_find(list, n1)->c);
      clearVar(returnVar);
      *returnVar = var1;
   }
   return OK;
}

// Make a copy of list "orig".  Shallow if "deep" is false.
// The refcount of the new list is set to 1.
// See item_copy() for "top" and "copyID". Return NULL when out of memory.
List *
list_copy(List *orig, int deep, int top, int copyID) {
   ListItem   *ni;

   if (!orig)
      return NULL;

   List* copy = list_alloc();

   if (orig->ty == NULL || top || deep)
      copy->ty = NULL;
   else
      copy->ty = alloc_type(orig->ty);
   if (copyID != 0) {
      // Do this before adding the items, because one of the items may
      // refer back to this list.
      orig->copyId = copyID;
      orig->copyList = copy;
   }
   CHECK_LIST_MATERIALIZE(orig);
   ListItem* item;
   for (item = orig->first; item != NULL && !gotInterruptG; item = item->next) {
      ni = listitem_alloc();
      if (deep) {
         if (item_copy(&item->c, &ni->c, deep, false, copyID) == FAIL) {
            eeglFree(ni);
            break;
         }
      } else
         copy_tv(OUT &ni->c, &item->c);
      list_append(copy, ni);
   }
   ++copy->refcount;
   if (item) {
     list_unref(copy);
     copy = NULL;
   }

   return copy;
}

//Remove items "item" to "item2" from list "l".
//Do not free the listitem or the value!
//This used to be called list_remove, but that conflicts with a Sun header file.
void
list_remove(List* l, ListItem* item, ListItem* item2) {
   ListItem* ip;

   CHECK_LIST_MATERIALIZE(l);

    // notify watchers
   for (ip = item; ip != NULL; ip = ip->next) {
      --l->len;
      list_fix_watch(l, ip);
      if (ip == item2)
         break;
   }

   if (item2->next == NULL)
      l->lv_u.mat.last = item->prev;
   else
      item2->next->prev = item->prev;
   if (item->prev == NULL)
      l->first = item2->next;
   else
      item->prev->next = item2->next;
   l->lv_u.mat.cachedItem = NULL;
}

// Return an allocated string with the string representation of a list. May return NULL.
CS
list2string(Var* tv, int copyID, int restore_copyID) {
   if (tv->list == NULL)
      return NULL;
   ArrayList   ga;
   ga_init2(&ga, sizeof(char), 80);
   ga_append(&ga, '[');
   CHECK_LIST_MATERIALIZE(tv->list);
   if (list_join(&ga, tv->list, (CS)", ", false, restore_copyID, copyID) == FAIL) {
      eeglFree(ga.c);
      return NULL;
   }
   ga_append(&ga, ']');
   ga_append(&ga, ZERO);
   return (CS)ga.c;
}

typedef struct join_S {
   CS s;
   Byte* tofree;
} Join;

private int
list_join_inner(
    ArrayList* gap,      // to store the result in
    List* l,
    CS sep,
    int echo_style,
    int restore_copyID,
    int copyID,
    ArrayList* join_gap)   // to keep each list item string
{
    int i;
    Join* p;
    int len;
    int sumlen = 0;
    int first = true;
    Byte* tofree;
    Byte numbuf[NUMBUFLEN];
    ListItem   *item;
    CS s;

   // Stringify each item in the list.
   CHECK_LIST_MATERIALIZE(l);
   for (item = l->first; item != NULL && !gotInterruptG; item = item->next) {
      s = echo_string_core(&item->c, &tofree, numbuf, copyID,
                     echo_style, restore_copyID, !echo_style);
      if (s == NULL)
          return FAIL;

      len = (int)STRLEN(s);
      sumlen += len;

      (void)ga_grow(join_gap, 1);
      p = ((Join *)join_gap->c) + (join_gap->len++);
      if (tofree != NULL || s != numbuf) {
          p->s = s;
          p->tofree = tofree;
      } else {
          p->s = copySubstr(s, len);
          p->tofree = p->s;
      }

      line_breakcheck();
      if (did_echo_string_emsg)  // recursion error, bail out
          break;
   }

    // Allocate result buffer with its total size, avoid re-allocation and
    // multiple copy operations.  Add 2 for a tailing ']' and ZERO.
   if (join_gap->len >= 2)
   sumlen += (int)STRLEN(sep) * (join_gap->len - 1);
   if (ga_grow(gap, sumlen + 2) == FAIL)
   return FAIL;

   for (i = 0; i < join_gap->len && !gotInterruptG; ++i) {
      if (first)
         first = false;
      else
         ga_concat(gap, sep);
      p = ((Join *)join_gap->c) + i;

      if (p->s != NULL)
          ga_concat(gap, p->s);
      line_breakcheck();
   }

    return OK;
}

// Join list "l" into a string in "*gap", using separator "sep".
// When "echo_style" is true use String as echoed, otherwise as inside a List.
// Return FAIL or OK.
int
list_join(
    ArrayList* gap,
    List* l,
    CS sep,
    int echo_style,
    int restore_copyID,
    int copyID
) {
   ArrayList   join_ga;

   if (l->len < 1)
      return OK; // nothing to do
   ga_init2(&join_ga, sizeof(Join), l->len);
   int retval = list_join_inner(gap, l, sep, echo_style, restore_copyID, copyID, &join_ga);

   if (join_ga.c == NULL)
      return retval;

   // Dispose each item in join_ga.
   Join* p = (Join *)join_ga.c;
   for (int i = 0; i < join_ga.len; ++i) {
      eeglFree(p->tofree);
      ++p;
   }
   ga_clear(&join_ga);

   return retval;
}

void
f_join(Arr(Var) argvars, Var* returnVar) {
   ArrayList   ga;

   returnVar->tag = VAR_STRING;

   if (check_for_list_arg(argvars, 0) == FAIL
          || check_for_opt_string_arg(argvars, 1) == FAIL)
      return;

   if ((argvars[0].tag == VAR_LIST && argvars[0].list == NULL))
      return;

   CS sep = (argvars[1].tag == VAR_UNKNOWN) ? S" " : convertVarToStringSingleUse(&argvars[1]);

   if (sep != NULL) {
      ga_init2(&ga, sizeof(char), 80);
      list_join(&ga, argvars[0].list, sep, true, false, 0);
      ga_append(&ga, ZERO);
      returnVar->string = (CS)ga.c;
   } else
      returnVar->string = NULL;
}

// Allocate a variable for a List and fill it from "*arg".
// "*arg" points to the "[". Return OK or FAIL.
int
eval_list(Byte **arg, Var* returnVar, EvalCtx *evalarg, int do_error) {
   int evaluate = evalarg == NULL ? false : evalarg->eval_flags & EVAL_EVALUATE;
   List* l = NULL;
   Var tv;
   ListItem   *item;
   int had_comma;

   if (evaluate) {
      l = list_alloc();
   }

   *arg = skipwhite_and_linebreak(*arg + 1, evalarg);
   while (**arg != ']' && **arg != ZERO) {
      if (eval1(arg, &tv, evalarg) == FAIL)   // recursive!
          goto failret;
      if (evaluate) {
         item = listitem_alloc();
         item->c = tv;
         item->c.lock = 0;
         list_append(l, item);
      }
      // Vim script allows a space before the comma.
      *arg = skipwhite(*arg);

      // the comma must come after the value
      had_comma = **arg == ',';
      if (had_comma) {
         *arg = skipwhite(*arg + 1);
      }

      // The "]" can be on the next line.  But a double quoted string may
      // follow, not a comment.
      *arg = skipwhite_and_linebreak(*arg, evalarg);
      if (**arg == ']')
          break;

      if (!had_comma) {
          if (do_error) {
         if (**arg == ',')
             showErrFmtMsg(_(e_no_white_space_allowed_before_str_str),
                               ",", *arg);
         else
             showErrFmtMsg(_(e_missing_comma_in_list_str), *arg);
          }
          goto failret;
      }
   }

   if (**arg != ']') {
   if (do_error)
       showErrFmtMsg(_(e_missing_end_of_list_rsb_str), *arg);
failret:
   if (evaluate)
       list_free(l);
   return FAIL;
   }

    *arg += 1;
   if (evaluate)
   returnVar_list_set(returnVar, l);

    return OK;
}

// Write "list" of strings to file "fd".
int
write_list(FILE* fd, List* list, int binary) {
   ListItem   *li;
   int c;
   int ret = OK;
   CS s;

   CHECK_LIST_MATERIALIZE(list);
   FOR_ALL_LIST_ITEMS(list, li) {
      for (s = tv_get_string(&li->c); *s != ZERO; ++s) {
         if (*s == '\n')
            c = putc(ZERO, fd);
         else
            c = putc(*s, fd);
         if (c == EOF) {
            ret = FAIL;
            break;
         }
      }
      if (!binary || li->next != NULL)
         if (putc('\n', fd) == EOF) {
            ret = FAIL;
            break;
         }
      if (ret == FAIL) {
         emsg(_(e_error_while_writing));
         break;
      }
   }
   return ret;
}

// Initialize a static list with 10 items.
void
init_static_list(StaticList10 *sl) {
   List* l = &sl->list;
   int i;

   CLEAR_POINTER(sl);
   l->first = &sl->items[0];
   l->lv_u.mat.last = &sl->items[9];
   l->refcount = DO_NOT_FREE_CNT;
   l->lock = VAR_FIXED;
   sl->list.len = 10;

   for (i = 0; i < 10; ++i) {
      ListItem *li = &sl->items[i];

      if (i == 0)
          li->prev = NULL;
      else
          li->prev = li - 1;
      if (i == 9)
          li->next = NULL;
      else
          li->next = li + 1;
   }
}

void
f_list2str(Arr(Var) argvars, Var* returnVar) {
   ListItem   *li;
   ArrayList   ga;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   List* l = argvars[0].list;
   if (!l)
      return;  // empty list results in empty string

   CHECK_LIST_MATERIALIZE(l);
   ga_init2(&ga, 1, 80);
   Byte buf[MB_MAXBYTES + 1];

   FOR_ALL_LIST_ITEMS(l, li) {
      buf[mb_char2bytes(tv_get_number(&li->c), buf)] = ZERO;
      ga_concat(&ga, buf);
   }
   ga_append(&ga, ZERO);

   returnVar->tag = VAR_STRING;
   returnVar->string = ga.c;
}

// Remove item argvars[1] from List argvars[0]. If argvars[2] is supplied, then
// remove the range of items from argvars[1] to argvars[2] (inclusive).
private void
listVar_remove(Arr(Var) argvars, Var* returnVar, CS arg_errmsg) {
   List* l;
   ListItem   *item, *item2;
   ListItem   *li;
   Boole error = false;
   long end;
   int cnt = 0;
   List   *rl;

   if ((l = argvars[0].list) == NULL || value_check_lock(l->lock, mbText(arg_errmsg), true))
      return;

   long idx = (long)varGetNumberChk(&argvars[1], OUT &error);
   if (error)
      return;      // type error: do nothing, errmsg already given

   if ((item = list_find(l, idx)) == NULL) {
      showErrFmtMsg(_(e_list_index_out_of_range_nr), idx);
      return;
   }

   if (argvars[2].tag == VAR_UNKNOWN) {
      // Remove one item, return its value.
      list_remove(l, item, item);
      *returnVar = item->c;
      list_free_item(l, item);
      return;
   }

   // Remove range of items, return list with values.
   end = (long)varGetNumberChk(&argvars[2], OUT &error);
   if (error)
      return;      // type error: do nothing

   if ((item2 = list_find(l, end)) == NULL) {
      showErrFmtMsg(_(e_list_index_out_of_range_nr), end);
      return;
   }

   for (li = item; li; li = li->next) {
      ++cnt;
      if (li == item2)
         break;
   }
   if (!li) { // didn't find "item2" after "item"
      emsg(_(e_invalid_range));
      return;
   }

   list_remove(l, item, item2);
   allocReturnList(returnVar);

   rl = returnVar->list;

   if (l->withItems > 0) {
      // need to copy the list items and move the value
      while (item) {
         li = listitem_alloc();
         li->c = item->c;
         initVarToNull(OUT &item->c);
         list_append(rl, li);
         if (item == item2)
            break;
         item = item->next;
      }
   } else {
      rl->first = item;
      rl->lv_u.mat.last = item2;
      item->prev = NULL;
      item2->next = NULL;
      rl->len = cnt;
   }
}

private int item_compare(const void *s1, const void *s2);
private int item_compare2(const void *s1, const void *s2);

// struct used in the array that's given to qsort()
typedef struct {
   ListItem   *item;
   int      idx;
} SortItem;

// struct storing information about current sort
typedef struct {
   int item_compare_ic;
   int item_compare_lc;
   int item_compare_numeric;
   int item_compare_numbers;
   int item_compare_float;
   CS item_compare_func;
   PartiallyApplied   *item_compare_partial;
   Bag* item_compare_selfdict;
   Boole item_compare_func_err;
   Boole item_compare_keep_zero;
} SortInfo;
private SortInfo* sortinfo = NULL;
#define ITEM_COMPARE_FAIL 999

// Comparer for f_sort() and f_uniq() below.
private int
item_compare(const void *s1, const void *s2) {
   Var   *tv1, *tv2;
   CS p1;
   CS p2;
   Byte   *tofree1 = NULL, *tofree2 = NULL;
   int      res;
   Byte   numbuf1[NUMBUFLEN];
   Byte   numbuf2[NUMBUFLEN];

   SortItem* si1 = (SortItem *)s1;
   SortItem* si2 = (SortItem *)s2;
   tv1 = &si1->item->c;
   tv2 = &si2->item->c;

   if (sortinfo->item_compare_numbers) {
      Long   v1 = tv_get_number(tv1);
      Long   v2 = tv_get_number(tv2);
      return v1 == v2 ? 0 : v1 > v2 ? 1 : -1;
   }

   if (sortinfo->item_compare_float) {
      double   v1 = tv_get_float(tv1);
      double   v2 = tv_get_float(tv2);

      return v1 == v2 ? 0 : v1 > v2 ? 1 : -1;
   }

   //tv2string() puts quotes around a string and allocates memory.  Don't do
   //that for string variables. Use a single quote when comparing with a
   //non-string to do what the docs promise.
   if (tv1->tag == VAR_STRING) {
      if (tv2->tag != VAR_STRING || sortinfo->item_compare_numeric)
         p1 = S"'";
      else
         p1 = tv1->string;
   } else
      p1 = tv2string(tv1, &tofree1, numbuf1, 0);
   if (tv2->tag == VAR_STRING) {
      if (tv1->tag != VAR_STRING || sortinfo->item_compare_numeric)
         p2 = S"'";
      else
         p2 = tv2->string;
   } else
      p2 = tv2string(tv2, &tofree2, numbuf2, 0);
   if (p1 == NULL)
      p1 = S"";
   if (p2 == NULL)
      p2 = S"";
   if (!sortinfo->item_compare_numeric) {
      if (sortinfo->item_compare_lc)
         res = strcoll((char *)p1, (char *)p2);
      else
         res = sortinfo->item_compare_ic ? caseInsensitiveCompare(p1, p2): STRCMP(p1, p2);
   } else {
      double n1, n2;
      n1 = strtod((char *)p1, (char **)&p1);
      n2 = strtod((char *)p2, (char **)&p2);
      res = n1 == n2 ? 0 : n1 > n2 ? 1 : -1;
   }

   // When the result would be zero, compare the item indexes. Makes the sort stable.
   if (res == 0 && !sortinfo->item_compare_keep_zero)
      res = si1->idx > si2->idx ? 1 : -1;

   eeglFree(tofree1);
   eeglFree(tofree2);
   return res;
}

private int
item_compare2(const void *s1, const void *s2) {
   Var   argv[3];
   PartiallyApplied   *partial = sortinfo->item_compare_partial;
   FnExe   funcexe;
   int      anyEmsgG_before = anyEmsgG;

   // shortcut after failure in previous call; compare all items equal
   if (sortinfo->item_compare_func_err)
      return 0;

   SortItem* si1 = (SortItem *)s1;
   SortItem* si2 = (SortItem *)s2;

   CS funcName = (!partial) ? sortinfo->item_compare_func : partial_name(partial);

   // Copy the values.  This is needed to be able to set lock to VAR_FIXED
   // in the copy without changing the original list items.
   copy_tv(OUT &argv[0], &si1->item->c);
   copy_tv(OUT &argv[1], &si2->item->c);

   Var returnVar;
   returnVar.tag = VAR_UNKNOWN;      // clearVar() uses this
   CLEAR_FIELD(funcexe);
   funcexe.fe_evaluate = true;
   funcexe.fe_partial = partial;
   funcexe.fe_selfdict = sortinfo->item_compare_selfdict;
   int res = call_func(funcName, -1, &returnVar, 2, argv, &funcexe);
   clearVar(&argv[0]);
   clearVar(&argv[1]);

   if (res == FAIL || anyEmsgG > anyEmsgG_before)
      res = ITEM_COMPARE_FAIL;
   else {
      res = (int)varGetNumberChk(&returnVar, OUT &sortinfo->item_compare_func_err);
      if (res > 0)
         res = 1;
      ei (res < 0)
         res = -1;
   }
   if (sortinfo->item_compare_func_err)
   res = ITEM_COMPARE_FAIL;  // return value has wrong type
    clearVar(&returnVar);

   //When the result would be zero, compare the pointers themselves. Makes the sort stable.
   if (res == 0 && !sortinfo->item_compare_keep_zero)
   res = si1->idx > si2->idx ? 1 : -1;

    return res;
}

private void
do_sort(List *l, SortInfo *info) {
   long   i = 0;
   ListItem   *li;

   long len = list_len(l);

   // Make an array with each entry pointing to an item in the List.
   Arr(SortItem) ptrs = ALLOC_MULT(SortItem, len);

   // sort(): ptrs will be the list to sort
   FOR_ALL_LIST_ITEMS(l, li) {
      ptrs[i].item = li;
      ptrs[i].idx = i;
      ++i;
   }

   info->item_compare_func_err = false;
   info->item_compare_keep_zero = false;
   // test the compare function
   if ((info->item_compare_func != NULL
         || info->item_compare_partial != NULL)
          && item_compare2((void *)&ptrs[0], (void *)&ptrs[1]) == ITEM_COMPARE_FAIL)
      emsg(_(e_sort_compare_function_failed));
   else {
      // Sort the array with item pointers.
      qsort((void *)ptrs, (Unt)len, sizeof(SortItem),
         info->item_compare_func == NULL
         && info->item_compare_partial == NULL
         ? item_compare : item_compare2);

      if (!info->item_compare_func_err) {
         // Clear the List and append the items in sorted order.
         l->first = l->lv_u.mat.last = l->lv_u.mat.cachedItem = NULL;
         l->len = 0;
         for (i = 0; i < len; ++i)
            list_append(l, ptrs[i].item);
      }
   }

    eeglFree(ptrs);
}

// uniq() List "l"
private void
do_uniq(List *l, SortInfo *info) {
   long   i = 0;
   ListItem   *li;
   int   (*item_compare_func_ptr)(const void *, const void *);

   long len = list_len(l);

   // Make an array with each entry pointing to an item in the List.
   Arr(SortItem) ptrs = ALLOC_MULT(SortItem, len);

   // f_uniq(): ptrs will be a stack of items to remove
   info->item_compare_func_err = false;
   info->item_compare_keep_zero = true;
   item_compare_func_ptr = info->item_compare_func || info->item_compare_partial 
       ? item_compare2 : item_compare;

   for (li = l->first; li != NULL && li->next != NULL; li = li->next) {
      if (item_compare_func_ptr((void *)&li, (void *)&li->next) == 0)
         ptrs[i++].item = li;
      if (info->item_compare_func_err) {
         emsg(_(e_uniq_compare_function_failed));
         break;
      }
   }

   if (!info->item_compare_func_err) {
      while (--i >= 0) {
         li = ptrs[i].item->next;
         ptrs[i].item->next = li->next;
         if (li->next)
            li->next->prev = ptrs[i].item;
         else
            l->lv_u.mat.last = ptrs[i].item;
         list_fix_watch(l, li);
         listitem_free(l, li);
         l->len--;
      }
   }

   eeglFree(ptrs);
}

// Parse the optional arguments supplied to the sort() or uniq() function and
// return the values in "info".
private int
parse_sort_uniq_args(Arr(Var) argvars, SortInfo *info) {
   info->item_compare_ic = false;
   info->item_compare_lc = false;
   info->item_compare_numeric = false;
   info->item_compare_numbers = false;
   info->item_compare_float = false;
   info->item_compare_func = NULL;
   info->item_compare_partial = NULL;
   info->item_compare_selfdict = NULL;

   if (argvars[1].tag == VAR_UNKNOWN)
      return OK;

    // optional second argument: {func}
   if (argvars[1].tag == VAR_FUNC)
      info->item_compare_func = argvars[1].string;
   ei (argvars[1].tag == VAR_PARTIAL)
      info->item_compare_partial = argvars[1].partial;
   else {
      Boole error = false;
      int       nr = 0;

      if (argvars[1].tag == VAR_NUMBER) {
         nr = varGetNumberChk(&argvars[1], OUT &error);
         if (error)
            return FAIL;
         if (nr == 1)
            info->item_compare_ic = true;
      }
      if (nr != 1) {
         if (argvars[1].tag != VAR_NUMBER)
            info->item_compare_func = tv_get_string(&argvars[1]);
         ei (nr != 0) {
            emsg(_(e_invalid_argument));
            return FAIL;
         }
      }
      if (info->item_compare_func != NULL) {
          if (*info->item_compare_func == ZERO) {
            // empty string means default sort
            info->item_compare_func = NULL;
          } ei (STRCMP(info->item_compare_func, "n") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_numeric = true;
          } ei (STRCMP(info->item_compare_func, "N") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_numbers = true;
          } ei (STRCMP(info->item_compare_func, "f") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_float = true;
          } ei (STRCMP(info->item_compare_func, "i") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_ic = true;
          } ei (STRCMP(info->item_compare_func, "l") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_lc = true;
          }
      }
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      // optional third argument: {dict}
      if (check_for_dict_arg(argvars, 2) == FAIL)
         return FAIL;
      info->item_compare_selfdict = argvars[2].bag;
   }

   return OK;
}

// "sort()" or "uniq()" function
private void
do_sort_uniq(Arr(Var) argvars, Var* returnVar, int sort) {
   SortInfo info;

   if (argvars[0].tag != VAR_LIST) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list), sort ? "sort()" : "uniq()");
      return;
   }

   // Pointer to current info struct used in compare function. Save and
   // restore the current one for nested calls.
   SortInfo* old_sortinfo = sortinfo;
   sortinfo = &info;

   List* l = argvars[0].list;
   if (l && value_check_lock(l->lock,
         sort ? text(N_("sort() argument")) : text(N_("uniq() argument")),
         true))
      goto theend;
   returnVar_list_set(returnVar, l);
   if (l == NULL)
      goto theend;
   CHECK_LIST_MATERIALIZE(l);

   long len = list_len(l);
   if (len <= 1)
      goto theend;   // short list sorts pretty quickly

   if (parse_sort_uniq_args(argvars, &info) == FAIL)
      goto theend;

   if (sort)
   do_sort(l, &info);
    else
   do_uniq(l, &info);

theend:
    sortinfo = old_sortinfo;
}

// "sort({list})" function
void
f_sort(Arr(Var) argvars, Var* returnVar) {
   do_sort_uniq(argvars, returnVar, true);
}

// "uniq({list})" function
void
f_uniq(Arr(Var) argvars, Var* returnVar) {
   do_sort_uniq(argvars, returnVar, false);
}

// Handle one item for map(), filter(), foreach(). Set v:val to "tv". Caller must set v:key.
int
filter_map_one(
   Var   *tv,         // original value
   Var   *expr,       // callback
   FilterMap filtermap,
   Var   *newtv,       // for map() and mapnew(): new value
   int      *remp      // for filter(): remove flag
){
   Var argv[3];
   int retval = FAIL;

   copy_tv(OUT get_EeglVar_tv(VV_VAL), tv);

   newtv->tag = VAR_UNKNOWN;
   if (filtermap == FILTERMAP_FOREACH && expr->tag == VAR_STRING) {
      // foreach() is not limited to an expression
      executeCommLine(expr->string);
      if (!anyEmsgG)
          retval = OK;
      goto theend;
   }

   argv[0] = *get_EeglVar_tv(VV_KEY);
   argv[1] = *get_EeglVar_tv(VV_VAL);
   if (eval_expr_typval(expr, false, argv, 2, newtv) == FAIL)
      goto theend;
   if (filtermap == FILTERMAP_FILTER) {
      Boole error = false;

      // filter(): when expr is zero remove the item
      *remp = (varGetNumberChk(newtv, OUT &error) == 0);
      clearVar(newtv);
      //On type error, nothing has been removed; return FAIL to stop the
      //loop. The error message was given by varGetNumberChk().
      if (error)
         goto theend;
   } ei (filtermap == FILTERMAP_FOREACH)
      clearVar(newtv);
   retval = OK;
theend:
   clearVar(get_EeglVar_tv(VV_VAL));
   return retval;
}

// Implementation of map(), filter(), foreach() for a List.  Apply "expr" to
// every item in List "l" and return the result in "returnVar".
private void
list_filter_map(
   List* l,
   FilterMap filtermap,
   CS arg_errmsg,
   Var* expr,
   Var* returnVar
) {
   List* l_ret = NULL;
   int idx = 0;
   int rem;
   ListItem* li;
   ListItem* nli;
   Var   newtv;

   if (filtermap == FILTERMAP_MAPNEW) {
      returnVar->tag = VAR_LIST;
      returnVar->list = NULL;
   }
   if (!l || (filtermap == FILTERMAP_FILTER && value_check_lock(l->lock, mbText(arg_errmsg), true)))
      return;

   int prev_lock = l->lock;

   if (filtermap == FILTERMAP_MAPNEW) {
      allocReturnList(returnVar);
      l_ret = returnVar->list;
   }
   // set_EeglVar_nr() doesn't set the type
   set_EeglVar_type(VV_KEY, VAR_NUMBER);

   if (l->lock == 0)
      l->lock = VAR_LOCKED;

   if (l->first == &range_list_item) {
      Long val = l->lv_u.nonmat.start;
      int len = l->len;
      int stride = l->lv_u.nonmat.stride;

      // List from range(): loop over the numbers
      // NOTE: foreach() returns the range_list_item
      if (filtermap != FILTERMAP_MAPNEW && filtermap != FILTERMAP_FOREACH) {
         l->first = NULL;
         l->lv_u.mat.last = NULL;
         l->len = 0;
         l->lv_u.mat.cachedItem = NULL;
      }

      for (idx = 0; idx < len; ++idx) {
         Var tv = (Var){.tag = VAR_NUMBER, .lock = 0, .number = val};

         set_EeglVar_nr(VV_KEY, idx);
         if (filter_map_one(&tv, expr, filtermap, &newtv, &rem) == FAIL)
            break;
         if (anyEmsgG) {
            clearVar(&newtv);
            break;
         }
         if (filtermap != FILTERMAP_FOREACH) {
            if (filtermap != FILTERMAP_FILTER) {
               // map(), mapnew(): always append the new value to the list
               if (list_append_tv_move(filtermap == FILTERMAP_MAP ? l : l_ret, &newtv) == FAIL)
                  break;
            } ei (!rem) {
               // filter(): append the list item value when not rem
               if (list_append_tv_move(l, &tv) == FAIL)
                  break;
            }
         }

         val += stride;
      }
   } else {
      // Materialized list: loop over the items
      for (li = l->first; li != NULL; li = nli) {
         if (filtermap == FILTERMAP_MAP && value_check_lock(li->c.lock, mbText(arg_errmsg), true))
            break;
         nli = li->next;
         set_EeglVar_nr(VV_KEY, idx);
         if (filter_map_one(&li->c, expr, filtermap, &newtv, &rem) == FAIL)
            break;
         if (anyEmsgG) {
            clearVar(&newtv);
            break;
         }
         if (filtermap == FILTERMAP_MAP) {
            // map(): replace the list item value
            clearVar(&li->c);
            newtv.lock = 0;
            li->c = newtv;
         } ei (filtermap == FILTERMAP_MAPNEW) {
            // mapnew(): append the list item value
            if (list_append_tv_move(l_ret, &newtv) == FAIL)
               break;
         } ei (filtermap == FILTERMAP_FILTER && rem)
            listitem_remove(l, li);
         ++idx;
      }
   }

   l->lock = prev_lock;
}

// Implementation of map(), filter() and foreach().
private void
filter_map(Arr(Var) argvars, Var* returnVar, FilterMap filtermap) {
    CS funcName = (CS)(
          filtermap == FILTERMAP_MAP 
             ? "map()"
             : filtermap == FILTERMAP_MAPNEW 
                ? "mapnew()"
                : filtermap == FILTERMAP_FILTER 
                   ? "filter()"
                   : "foreach()"
   );
   CS arg_errmsg = (CS)(
         filtermap == FILTERMAP_MAP
                ? N_("map() argument")
                : filtermap == FILTERMAP_MAPNEW
                     ? N_("mapnew() argument")
                     : filtermap == FILTERMAP_FILTER
                        ? N_("filter() argument")
                        : N_("foreach() argument")
   );
   int      save_anyEmsgG;

   // map(), filter(), foreach() return the first argument, also on failure.
   if (filtermap != FILTERMAP_MAPNEW && argvars[0].tag != VAR_STRING)
      copy_tv(OUT returnVar, &argvars[0]);

   if (argvars[0].tag != VAR_BLOB
       && argvars[0].tag != VAR_LIST
       && argvars[0].tag != VAR_BAG
       && argvars[0].tag != VAR_STRING
   ) {
      CS msg;
      if (filtermap == FILTERMAP_FOREACH)
         msg = e_argument_of_str_must_be_list_tuple_string_dictionary_or_blob;
      else
         msg = e_argument_of_str_must_be_list_string_dictionary_or_blob;
      showErrFmtMsg(_(msg), funcName);
      return;
   }

   // On type errors, the preceding call has already displayed an error message. Avoid a 
   // misleading error message for an empty string that was not passed as argument.
   Var* expr = &argvars[1];
   if (expr->tag == VAR_UNKNOWN)
      return;

   Var   save_val;
   Var   save_key;

   prepareEeglVar(VV_VAL, &save_val);
   prepareEeglVar(VV_KEY, &save_key);

   // We reset "anyEmsgG" to be able to detect whether an error
   // occurred during evaluation of the expression.
   save_anyEmsgG = anyEmsgG;
   anyEmsgG = false;

   if (argvars[0].tag == VAR_BAG)
      bagFilterMap(argvars[0].bag, filtermap, arg_errmsg, expr, returnVar);
   ei (argvars[0].tag == VAR_BLOB)
      blob_filter_map(argvars[0].blob, filtermap, expr, arg_errmsg, returnVar);
   ei (argvars[0].tag == VAR_STRING)
      string_filter_map(tv_get_string(&argvars[0]), filtermap, expr, returnVar);
   else // argvars[0].tag == VAR_LIST
      list_filter_map(argvars[0].list, filtermap, arg_errmsg, expr, returnVar);

   restoreEeglVar(VV_KEY, &save_key);
   restoreEeglVar(VV_VAL, &save_val);

   anyEmsgG |= save_anyEmsgG;
}

void
f_filter(Arr(Var) argvars, Var* returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_FILTER);
}

void
f_map(Arr(Var) argvars, Var* returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_MAP);
}

void
f_mapnew(Arr(Var) argvars, Var* returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_MAPNEW);
}

void
f_foreach(Arr(Var) argvars, Var* returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_FOREACH);
}

// "add(list, item)" function
private void
list_add(Arr(Var) argvars, Var* returnVar) {
   List   *l = argvars[0].list;

   if (!l) {
   } ei (!value_check_lock(l->lock, text(N_("add() argument")), true)
          && list_append_tv(l, &argvars[1]) == OK
   ) {
      copy_tv(OUT returnVar, &argvars[0]);
   }
}

// "add(object, item)" function
void
f_add(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = 1; // Default: Failed

   if (argvars[0].tag == VAR_LIST)
      list_add(argvars, returnVar);
   ei (argvars[0].tag == VAR_BLOB)
      blob_add(argvars, returnVar);
   else
      emsg(_(e_list_or_blob_required));
}

//Count the number of times item "needle" occurs in List "l" starting at index
//"idx". Case is ignored if "ic" is true.
private long
list_count(List *l, Var *needle, long idx, int ic) {
   long   n = 0;

   if (!l)
      return 0;

   CHECK_LIST_MATERIALIZE(l);

   if (list_len(l) == 0)
      return 0;

   ListItem* li = list_find(l, idx);
   if (!li) {
      showErrFmtMsg(_(e_list_index_out_of_range_nr), idx);
      return 0;
   }

   for ( ; li; li = li->next) {
      if (tv_equal(&li->c, needle, ic))
         ++n;
   } 

   return n;
}

void
f_count(Arr(Var) argvars, Var* returnVar) {
   long   n = 0;
   int      ic = false;
   Boole error = false;

   if (argvars[2].tag != VAR_UNKNOWN)
      ic = (int)varGetNumberChk(argvars + 2, OUT &error);

   if (!error && argvars[0].tag == VAR_STRING)
      n = string_count(argvars[0].string, convertVarToStringSingleUse(&argvars[1]), ic);
   ei (!error && argvars[0].tag == VAR_LIST) {
      long idx = 0;

   if (argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN)
      idx = (long)varGetNumberChk(argvars + 3, OUT &error);
   if (!error)
      n = list_count(argvars[0].list, &argvars[1], idx, ic);
   } ei (!error && argvars[0].tag == VAR_BAG) {
      if (argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN)
         emsg(_(e_invalid_argument));
      else
         n = bagCount(argvars[0].bag, &argvars[1], ic);
   } ei (!error)
      showErrFmtMsg(_(e_argument_of_str_must_be_list_string_or_dictionary), "count()");
   returnVar->number = n;
}

// extend() a List. Append List argvars[1] to List argvars[0] before index
// argvars[3] and return the resulting list in "returnVar".  "is_new" is true for extendnew().
private void
list_extend_func(
   Var   *argvars,
   CS arg_errmsg,
   int      is_new,
   Var   *returnVar
) {
   ListItem   *item;
   long before;
   Boole error = false;

   List* l1 = argvars[0].list;
   if (!l1) {
      emsg(_(e_cannot_extend_null_list));
      return;
   }
   List* l2 = argvars[1].list;
   if (!(is_new || !value_check_lock(l1->lock, mbText(arg_errmsg), true)) || !l2) {
      return;
   } 
   if (is_new) {
      l1 = list_copy(l1, false, true, get_copyID());
      if (!l1)
         return;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      before = (long)varGetNumberChk(argvars + 2, OUT &error);
      if (error)
         return;      // type error; errmsg already given

      if (before == l1->len)
         item = NULL;
      else {
         item = list_find(l1, before);
         if (!item) {
            showErrFmtMsg(_(e_list_index_out_of_range_nr), before);
            return;
         }
      }
   } else
      item = NULL;
   list_extend(l1, l2, item);

   if (is_new) {
      *returnVar = (Var){.tag = VAR_LIST, .list = l1, .lock = false};
   } else
      copy_tv(OUT returnVar, &argvars[0]);
}

// "extend()" or "extendnew()" function.  "is_new" is true for extendnew().
private void
extend(Arr(Var) argvars, Var* returnVar, CS arg_errmsg, int is_new) {
   CS funcName = (CS)( is_new ? "extendnew()" : "extend()");

   if (argvars[0].tag == VAR_LIST && argvars[1].tag == VAR_LIST) {
      // Check that extend() does not change the type of the list if it was declared.
      list_extend_func(argvars, arg_errmsg, is_new, returnVar);
   } ei (argvars[0].tag == VAR_BAG && argvars[1].tag == VAR_BAG) {
      // Check that extend() does not change the type of the dict if it was declared.
      bagExtend_func(argvars, arg_errmsg, is_new, returnVar);
   } else
      showErrFmtMsg(_(e_argument_of_str_must_be_list_or_dictionary), funcName);
}

// "extend(list, list [, idx])" function. "extend(dict, dict [, action])" function
void
f_extend(Arr(Var) argvars, Var* returnVar) {
   CS errmsg = (CS)N_("extend() argument");
   extend(argvars, returnVar, errmsg, false);
}

// "extendnew(list, list [, idx])" function. "extendnew(dict, dict [, action])" function
void
f_extendnew(Arr(Var) argvars, Var* returnVar) {
   CS errmsg = (CS)N_("extendnew() argument");
   extend(argvars, returnVar, errmsg, true);
}

private void
list_insert_func(Arr(Var) argvars, Var* returnVar) {
   List* l = argvars[0].list;
   long before = 0;
   ListItem   *item;
   Boole error = false;

   if (!l) {
      return;
   }

   if (value_check_lock(l->lock, text(N_("insert() argument")), true))
      return;

   if (argvars[2].tag != VAR_UNKNOWN)
      before = (long)varGetNumberChk(argvars + 2, OUT &error);
   if (error)
      return;      // type error; errmsg already given

   if (before == l->len)
      item = NULL;
   else {
      item = list_find(l, before);
      if (item == NULL) {
         showErrFmtMsg(_(e_list_index_out_of_range_nr), before);
         l = NULL;
      }
   }
   if (l) {
      (void)list_insert_tv(l, &argvars[1], item);
      copy_tv(OUT returnVar, &argvars[0]);
   }
}

void
f_insert(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_BLOB)
      blob_insert_func(argvars, returnVar);
   ei (argvars[0].tag != VAR_LIST)
      showErrFmtMsg(_(e_argument_of_str_must_be_list_or_blob), "insert()");
   else
      list_insert_func(argvars, returnVar);
}

void
f_remove(Arr(Var) argvars, Var* returnVar) {
   CS arg_errmsg = (CS)N_("remove() argument");

   if (argvars[0].tag == VAR_BAG)
      bagRemove(argvars, returnVar, arg_errmsg);
   ei (argvars[0].tag == VAR_BLOB)
      blob_remove(argvars, returnVar, arg_errmsg);
   ei (argvars[0].tag == VAR_LIST)
      listVar_remove(argvars, returnVar, arg_errmsg);
   else
      showErrFmtMsg(_(e_argument_of_str_must_be_list_dictionary_or_blob), "remove()");
}

private void
list_reverse(List *l, Var* returnVar) {
   ListItem   *li, *ni;

   returnVar_list_set(returnVar, l);
   if (l && !value_check_lock(l->lock, text(N_("reverse() argument")), true)) {
      if (l->first == &range_list_item) {
         Long new_start = l->lv_u.nonmat.start + ((Long)l->len - 1) * l->lv_u.nonmat.stride;
         l->lv_u.nonmat.end = new_start - (l->lv_u.nonmat.end - l->lv_u.nonmat.start);
         l->lv_u.nonmat.start = new_start;
         l->lv_u.nonmat.stride = -l->lv_u.nonmat.stride;
         return;
      }
      li = l->lv_u.mat.last;
      l->first = l->lv_u.mat.last = NULL;
      l->len = 0;
      while (li) {
         ni = li->prev;
         list_append(l, li);
         li = ni;
      }
      l->lv_u.mat.cachedInd = l->len - l->lv_u.mat.cachedInd - 1;
   }
}

// "reverse({list})" function
void
f_reverse(Arr(Var) argvars, Var* returnVar) {
   if (check_for_string_or_list_or_blob_arg(argvars, 0) == FAIL)
      return;

   if (argvars[0].tag == VAR_BLOB)
      blob_reverse(argvars[0].blob, returnVar);
   ei (argvars[0].tag == VAR_STRING) {
      returnVar->tag = VAR_STRING;
   if (argvars[0].string != NULL)
      returnVar->string = reverse_text(argvars[0].string);
   else
       returnVar->string = NULL;
   } ei (argvars[0].tag == VAR_LIST)
      list_reverse(argvars[0].list, returnVar);
}

// Implementation of reduce() for list "argvars[0]", using the function "expr"
// starting with the optional initial value argvars[2] and return the result in "returnVar".
private void
list_reduce(
   Var   *argvars,
   Var   *expr,
   Var   *returnVar)
{
   List   *l = argvars[0].list;
   ListItem  *li = NULL;
   int      range_idx = 0;
   Long   range_val = 0;
   Var   initial;
   Var   argv[3];
   int      r;
   int      called_emsg_start = called_emsg;
   int      prev_locked;

   // Using reduce on a range() uses "range_idx" and "range_val".
   int range_list = l != NULL && l->first == &range_list_item;
   if (range_list)
      range_val = l->lv_u.nonmat.start;

   if (argvars[2].tag == VAR_UNKNOWN) {
      if (!l || l->len == 0) {
         showErrFmtMsg(_(e_reduce_of_an_empty_str_with_no_initial_value), "List");
         return;
      }
      if (range_list) {
         initial.tag = VAR_NUMBER;
         initial.number = range_val;
         range_val += l->lv_u.nonmat.stride;
         range_idx = 1;
      } else {
         initial = l->first->c;
         li = l->first->next;
      }
   } else {
      initial = argvars[2];
      if (l && !range_list)
         li = l->first;
   }
   copy_tv(OUT returnVar, &initial);

   if (l == NULL)
      return;

   prev_locked = l->lock;
   l->lock = VAR_FIXED;  // disallow the list changing here

   while (range_list ? range_idx < l->len : li != NULL) {
      argv[0] = *returnVar;
      returnVar->tag = VAR_UNKNOWN;

      if (range_list) {
         argv[1].tag = VAR_NUMBER;
         argv[1].number = range_val;
      } else
         argv[1] = li->c;

      r = eval_expr_typval(expr, true, argv, 2, returnVar);

      if (argv[0].tag != VAR_NUMBER && argv[0].tag != VAR_UNKNOWN)
          clearVar(&argv[0]);
      if (r == FAIL || called_emsg != called_emsg_start)
          break;

      // advance to the next item
      if (range_list) {
         range_val += l->lv_u.nonmat.stride;
         ++range_idx;
      } else
         li = li->next;
   }

   l->lock = prev_locked;
}

//"reduce(list, { accumulator, element -> value } [, initial])" function
//"reduce(blob, { accumulator, element -> value } [, initial])"
//"reduce(string, { accumulator, element -> value } [, initial])"
void
f_reduce(Arr(Var) argvars, Var* returnVar) {
   CS funcName;

   if (check_for_string_or_list_or_blob_arg(argvars, 0) == FAIL)
      return;

   if (argvars[1].tag == VAR_FUNC)
      funcName = argvars[1].string;
   ei (argvars[1].tag == VAR_PARTIAL)
      funcName = partial_name(argvars[1].partial);
   else
      funcName = tv_get_string(&argvars[1]);
   if (funcName == NULL || *funcName == ZERO) {
      emsg(_(e_missing_function_argument));
      return;
   }

   if (argvars[0].tag == VAR_LIST)
      list_reduce(argvars, &argvars[1], returnVar);
   ei (argvars[0].tag == VAR_STRING)
      string_reduce(argvars, &argvars[1], returnVar);
   else
      blob_reduce(argvars, &argvars[1], returnVar);
}

void
f_slice(Arr(Var) argvars, Var* returnVar) {
   if (check_can_index(&argvars[0], true, false) != OK)
      return;

   copy_tv(OUT returnVar, argvars);
   eval_index_inner(
      returnVar, true, argvars + 1,
      argvars[2].tag == VAR_UNKNOWN ? NULL : argvars + 2, true, NULL, 0, false
   );
}

// Same as ga_grow() but uses an allocation id for testing.
int
ga_grow_id(ArrayList *gap, int n, AllocId id) {
   if (alloc_fail_id == id && alloc_does_fail(sizeof(List)))
      return FAIL;

   return ga_grow(gap, n);
}


//}}}
//{{{hashtable: Handling of a hashtable with Eegl-specific properties.

// Each item in a hashtable has a ZERO-terminated string key.  A key can appear only once in 
// the table.
//
// A hash number is computed from the key for quick lookup.  When the hashes of two different keys 
// point to the same entry an algorithm is used to iterate over other entries in the table until 
// the right one is found. To make the iteration work removed keys are different from entries where
// a key was never present.
//
// The mechanism has been partly based on how Python Dictionaries are implemented. The algorithm 
// is from Knuth Vol. 3, Sec. 6.4.
//
// The hashtable grows to accommodate more entries when needed.  At least 1/3 of the entries is 
// empty to keep the lookup efficient (at the cost of extra memory).

#if 0
# define HT_DEBUG   // extra checks for table consistency  and statistics

private long hash_count_lookup = 0;   // count number of hashtab lookups
private long hash_count_perturb = 0;   // count number of "misses"
#endif

// Magic value for algorithm that walks through the array.
#define PERTURB_SHIFT 5

private int hash_may_resize(EeSet *ht, int minitems);

#if 0 // currently not used
// Create an empty hash table. Return NULL when out of memory.
EeSet *
hash_create(void) {
   EeSet *ht;

   ht = ALLOC_ONE(EeSet);
   if (ht != NULL)
      hash_init(ht);
   return ht;
}
#endif

// Initialize an empty hash table.
void
hash_init(EeSet* ht) {
   // This zeroes all "ht_" entries and all the "hi_key" in "smallArray".
   CLEAR_POINTER(ht);
   ht->array = ht->smallArray;
   ht->mask = HT_INIT_SIZE - 1;
}

// If "ht->flags" has HTFLAGS_FROZEN then give an error message using "command" and return true.
int
check_hashtab_frozen(EeSet* ht, CS command) {
   if ((ht->flags & HTFLAGS_FROZEN) == 0)
      return false;

   showErrFmtMsg(_(e_not_allowed_to_add_or_remove_entries_str), command);
      return true;
}

// Free the array of a hash table.  Does not free the items it contains!
// If "ht" is not freed then you should call hash_init() next!
void
hash_clear(EeSet* ht) {
   if (ht->array != ht->smallArray)
      eeglFree(ht->array);
}

// Free the array of a hash table and all the keys it contains.  The keys must have been allocated.
// "off" is the offset from the start of the allocate memory to the location of the key (it's 
// always positive).
void
hash_clear_all(EeSet* ht, int off) {
   EeSetItem   *hi;

   long todo = (long)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         eeglFree(hi->hi_key - off);
         --todo;
      }
   }
   hash_clear(ht);
}

// Find "key" in hashtable "ht". "key" must not be NULL. Always return a pointer to a EeSetItem.
// If the item was not found, then HASHITEM_EMPTY() is true.  The pointer is then the place where
// the key would be added. WARNING: The returned pointer becomes invalid when the hashtable is 
// changed (adding, setting or removing an item)!
EeSetItem *
hash_find(EeSet* ht, Text key) {
   return hash_lookup(ht, key, calcHash(key));
}

// Like hash_find(), but caller computes "hash".
EeSetItem *
hash_lookup(EeSet* ht, Text key, Hash hash) {
   Hash   perturb;

#ifdef HT_DEBUG
   ++hash_count_lookup;
#endif

   // Quickly handle the most common situations:
   // - return if there is no item at all
   // - skip over a removed item
   // - return if the item matches
   Unt idx = (Unt)(hash & ht->mask);
   EeSetItem* hi = &ht->array[idx];

   if (hi->len == 0)
      return hi;
      
   EeSetItem* freeitem;
   if (hi->hi_key == HI_KEY_REMOVED)
      freeitem = hi;
   ei (hi->hi_hash == hash && STRNCMP(hi->hi_key, key.c, key.len) == 0)
      return hi;
   else
      freeitem = NULL;

   //Need to search through the table to find the key. The algorithm to step through the table 
   //starts with large steps, gradually becoming smaller down to (1/4 table size + 1). This 
   //means it goes through all table entries in the end. When we run into a NULL key it's clear 
   //that the key isn't there. Return the first available slot found (can be a slot of a 
   //removed item).
   for (perturb = hash; ; perturb >>= PERTURB_SHIFT) {
#ifdef HT_DEBUG
      ++hash_count_perturb;       // count a "miss" for hashtab lookup
#endif
      idx = (Unt)((idx << 2U) + idx + perturb + 1);
      hi = &ht->array[idx & ht->mask];
      if (hi->len == 0)
         return freeitem ? freeitem : hi;
      if (hi->hi_hash == hash
            && hi->hi_key != HI_KEY_REMOVED
            && STRNCMP(hi->hi_key, key.c, key.len) == 0)
         return hi;
      if (hi->hi_key == HI_KEY_REMOVED && !freeitem)
         freeitem = hi;
   }
   return null;
}

// Print the efficiency of hashtable lookups. Useful when trying different hash algorithms.
// Called when exiting.
void
hash_debug_results(void) {
# ifdef HT_DEBUG
   fprintf(stderr, "\r\n\r\n\r\n\r\n");
   fprintf(stderr, "Number of hashtable lookups: %ld\r\n", hash_count_lookup);
   fprintf(stderr, "Number of perturb loops: %ld\r\n", hash_count_perturb);
   fprintf(stderr, "Percentage of perturb loops: %ld%%\r\n",
            hash_count_perturb * 100 / hash_count_lookup);
# endif
}

//Add item with key "key" to hashtable "ht". "command" is used for the error message when the 
//hashtab if frozen. Return FAIL when out of memory or the key is already present.
int
hash_add(EeSet* ht, Text key, CS command) {
   Hash hash = calcHash(key);

   if (check_hashtab_frozen(ht, command))
      return FAIL;
   EeSetItem* hi = hash_lookup(ht, key, hash);
   if (!HASHITEM_EMPTY(hi)) {
      internal_error((CS)"hash_add()");
      return FAIL;
   }
   return hash_add_item(ht, hi, key, hash);
}

// Add item "hi" with "key" to hashtable "ht". "key" must not be NULL and "hi" must have been 
// obtained with hash_lookup() and point to an empty item. "hi" is invalid after this!
// Return OK or FAIL (out of memory).
int
hash_add_item(EeSet* ht, EeSetItem* hi, Text newItem, Hash hash) {
   // If resizing failed before and it fails again we can't add an item.
   if (ht->flags & HTFLAGS_ERROR)
      return FAIL;

   ++ht->count;
   ++ht->changes;
   if (hi->len == 0)
      ++ht->occupied;
   hi->hi_key = newItem.c;
   hi->len = newItem.len;
   hi->hi_hash = hash;

   // When the space gets low may resize the array.
   return hash_may_resize(ht, 0);
}

#if 0  // not used
// Overwrite hashtable item "hi" with "key".  "hi" must point to the item that
// is to be overwritten.  Thus the number of items in the hashtable doesn't change.
// Although the key must be identical, the pointer may be different, thus it's
// set anyway (the key is part of an item with that key).
// The caller must take care of freeing the old item.
// "hi" is invalid after this!
void
hash_set(EeSetItem *hi, Byte *key) {
   hi->hi_key = key;
}
#endif

//Remove item "hi" from  hashtable "ht". "hi" must have been obtained with hash_lookup().
//"command" is used for the error message when the hashtab if frozen.
//The caller must take care of freeing the item itself.
int
hash_remove(EeSet* ht, EeSetItem* hi, CS command) {
   if (check_hashtab_frozen(ht, command))
      return FAIL;
   --ht->count;
   ++ht->changes;
   hi->hi_key = HI_KEY_REMOVED;
   hi->len = 0;
   hash_may_resize(ht, 0);
   return OK;
}

// Lock a hashtable: prevent that array changes. Don't use this when items are to be added!
// Must call hash_unlock() later.
void
hash_lock(EeSet *ht) {
    ++ht->ht_locked;
}

// Lock a hashtable at the specified number of entries.
// Caller must make sure no more than "size" entries will be added. Must call hash_unlock() later.
void
hash_lock_size(EeSet *ht, int size) {
    (void)hash_may_resize(ht, size);
    ++ht->ht_locked;
}

// Unlock a hashtable: allow array changes again. Table will be resized (shrink) when necessary.
// This must balance a call to hash_lock().
void
hash_unlock(EeSet* ht) {
   --ht->ht_locked;
   (void)hash_may_resize(ht, 0);
}

// Shrink a hashtable when there is too much empty space. Grow a hashtable when there is not 
// enough empty space. Return OK or FAIL (overflow on size).
private int
hash_may_resize(EeSet* ht, int minitems) {     // minimal number of items
   EeSetItem temparray[HT_INIT_SIZE];
   EeSetItem *oldarray, *newarray;
   EeSetItem *olditem, *newitem;
   Unt newi;
   int      todo;
   Ulong   newsize;
   Ulong   minsize;
   Ulong   newmask;
   Hash   perturb;

   // Don't resize a locked table.
   if (ht->ht_locked > 0)
      return OK;

#ifdef HT_DEBUG
   if (ht->count > ht->occupied)
      emsg("hash_may_resize(): more used than filled");
   if (ht->occupied >= ht->mask + 1)
      emsg("hash_may_resize(): table completely filled");
#endif

   Ulong oldsize = ht->mask + 1;
   if (minitems == 0) {
      // Return quickly for small tables with at least two NULL items.  NULL
      // items are required for the lookup to decide a key isn't there.
      if (ht->occupied < HT_INIT_SIZE - 1 && ht->array == ht->smallArray)
          return OK;

      //Grow or refill the array when it's more than 2/3 full (including
      //removed items, so that they get cleaned up).
      //Shrink the array when it's less than 1/5 full.  When growing it is
      //at least 1/4 full (avoids repeated grow-shrink operations)
      if (ht->occupied * 3 < oldsize * 2 && ht->count > oldsize / 5)
         return OK;

      if (ht->count > 1000)
          minsize = ht->count * 2;  // it's big, don't make too much room
      else
          minsize = ht->count * 4;  // make plenty of room
   } else {
      // Use specified size.
      if ((Ulong)minitems < ht->count)   // just in case...
          minitems = (int)ht->count;
      minsize = (minitems * 3 + 1) / 2;   // array is up to 2/3 full
   }

   newsize = HT_INIT_SIZE;
   while (newsize < minsize) {
      newsize <<= 1;      // make sure it's always a power of 2
      if (newsize == 0)
         return FAIL;   // overflow
   }

   if (newsize == HT_INIT_SIZE) {
      // Use the small array inside the hashdict structure.
      newarray = ht->smallArray;
      if (ht->array == newarray) {
         // Moving from smallArray to smallArray!  Happens when there
         // are many removed items.  Copy the items to be able to clean up removed items.
         MEMMOVE(temparray, newarray, sizeof(temparray));
         oldarray = temparray;
      } else
         oldarray = ht->array;
      CLEAR_FIELD(ht->smallArray);
   } ei (newsize == oldsize && ht->occupied * 3 < oldsize * 2) {
      //The hashtab is already at the desired size, and there are not too
      //many removed items, bail out.
      return OK;
   } else {
      // Allocate an array.
      newarray = ALLOC_CLEAR_MULT(EeSetItem, newsize);
      oldarray = ht->array;
   }

   //Move all the items from the old array to the new one, placing them in
   //the right spot.  The new array won't have any removed items, thus this
   //is also a cleanup action.
   newmask = newsize - 1;
   todo = (int)ht->count;
   for (olditem = oldarray; todo > 0; ++olditem) {
      if (!HASHITEM_EMPTY(olditem)) {
         //The algorithm to find the spot to add the item is identical to
         //the algorithm to find an item in hash_lookup().  But we only
         //need to search for a NULL key, thus it's simpler.
         newi = (unsigned)(olditem->hi_hash & newmask);
         newitem = &newarray[newi];

         if (newitem->len > 0) {
            for (perturb = olditem->hi_hash; ; perturb >>= PERTURB_SHIFT) {
               newi = (unsigned)((newi << 2U) + newi + perturb + 1U);
               newitem = &newarray[newi & newmask];
               if (newitem->len == 0)
                  break;
            }
         }
         *newitem = *olditem;
         --todo;
      }
   } 

   if (ht->array != ht->smallArray)
      eeglFree(ht->array);
   ht->array = newarray;
   ht->mask = newmask;
   ht->occupied = ht->count;
   ++ht->changes;
   ht->flags &= ~HTFLAGS_ERROR;

   return OK;
}

//Get the hash number for a key. If you think you know a better hash function: Compile with 
//HT_DEBUG set and run a script that uses hashtables a lot. Eegl will then print statistics when 
//exiting. Try that with the current hash algorithm and yours. The lower the percentage the better.
Hash
calcHash(Text const key) {
   Hash hash;
   if ((hash = key.c[0]) == ZERO)
      return (Hash)0;
   CS p = key.c + 1;

   // A simplistic algorithm that appears to do very well. Suggested by George Reilly.
   for (; p < key.c + key.len; p++)
      hash = hash * 101 + (*p);

   return hash;
}

//}}}
//{{{ExpandMatch and Fuzzy

//"al" must be an arraylist of CS!
ExpandMatch
expandMatchOfArrayList(ArrayList al) {
   return (ExpandMatch){.c = al.c, .len = al.len};
}

void
addExpandMatch(CS m, OUT ExpandMatch* t) {
   if (t->len == t->cap) {
      Arr(CS) newContent = allocateArray(t->cap*2, CS, t->a);
      if (t->len > 0)
         memcpy(newContent, t->c, t->len*sizeof(CS));
      t->c = newContent;
      t->cap *= 2;
   }
   t->c[t->len] = m;
   t->len++;
}

//}}}
//{{{Scripting variables (the tagged data accessible from scripts)
//{{{Variables

//Return true if "type" is NULL, any or unknown.
//This also works for const (comparing with &t_any and &t_unknown doesn't).
int
type_any_or_unknown(TypeSpec *type) {
   return type == NULL || type->tag == VAR_ANY   || type->tag == VAR_UNKNOWN;
}

Arr(char)
vartype_name(VarTag type) {
   switch (type) {
   case VAR_UNKNOWN: break;
   case VAR_ANY: return "any";
   case VAR_VOID: return "void";
   case VAR_SPECIAL: return "special";
   case VAR_BOOL: return "bool";
   case VAR_NUMBER: return "number";
   case VAR_FLOAT: return "float";
   case VAR_STRING: return "string";
   case VAR_BLOB: return "blob";
   case VAR_JOB: return "job";
   case VAR_CHANNEL: return "channel";
   case VAR_LIST: return "list";
   case VAR_BAG: return "dict";
   case VAR_FUNC:
   case VAR_PARTIAL: return "func";
   }
   return "unknown";
}

// Allocate memory for a variable type-value, and make it empty (0 or NULL value)
Var *
allocVar(void) {
   return ALLOC_CLEAR_ONE(Var);
}

//Allocate memory for a variable type-value, and assign a string to it.
//The string "s" must have been allocated, it is consumed.
//Return NULL for out of memory, the variable otherwise.
Var *
allocStringVar(CS s) {
   Var* returnVar = allocVar();
   returnVar->tag = VAR_STRING;
   returnVar->string = s;
   return returnVar;
}

//Free the memory for a variable type-value.
void
freeVar(Var* varp) {
   if (!varp)
      return;

   switch (varp->tag) {
   case VAR_FUNC:
      func_unref(varp->string);
      // FALLTHROUGH
   case VAR_STRING:
      eeglFree(varp->string);
      break;
   case VAR_PARTIAL:
      partial_unref(varp->partial);
      break;
   case VAR_BLOB:
      blob_unref(varp->blob);
      break;
   case VAR_LIST:
      list_unref(varp->list);
      break;
   case VAR_BAG:
      bagUnref(varp->bag);
      break;
   case VAR_JOB:
      job_unref(varp->job);
      break;
   case VAR_CHANNEL:
      channel_unref(varp->channel);
      break;
   case VAR_NUMBER:
   case VAR_FLOAT:
   case VAR_ANY:
   case VAR_UNKNOWN:
   case VAR_VOID:
   case VAR_BOOL:
   case VAR_SPECIAL:
       break;
   }
   eeglFree(varp);
}

//Free the memory for a variable value and set the value to NULL or 0.
void
clearVar(Var* varp) {
   if (varp == NULL)
   return;

   switch (varp->tag) {
   case VAR_FUNC:
      func_unref(varp->string);
      // FALLTHROUGH
   case VAR_STRING:
      EE_CLEAR(varp->string);
      break;
   case VAR_PARTIAL:
      partial_unref(varp->partial);
      varp->partial = NULL;
      break;
   case VAR_BLOB:
      blob_unref(varp->blob);
      varp->blob = NULL;
      break;
   case VAR_LIST:
      list_unref(varp->list);
      varp->list = NULL;
      break;
   case VAR_BAG:
      bagUnref(varp->bag);
      varp->bag = NULL;
      break;
   case VAR_NUMBER:
   case VAR_BOOL:
   case VAR_SPECIAL:
      varp->number = 0;
      break;
   case VAR_FLOAT:
      varp->floatt = 0.0;
      break;
   case VAR_JOB:
      job_unref(varp->job);
      varp->job = NULL;
      break;
   case VAR_CHANNEL:
      channel_unref(varp->channel);
      varp->channel = NULL;
      break;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
       break;
   }
   varp->lock = 0;
}

//Set the value of a variable to NULL without freeing items.
void
initVarToNull(OUT Var* varp) {
   if (varp)
      CLEAR_POINTER(varp);
}

private Long
convertToBoolOrNumber(
   Var* varp,
   OUT Boole* denote
){
   Long   n = 0L;

   switch (varp->tag) {
   case VAR_NUMBER:
      return varp->number;
   case VAR_FLOAT:
      emsg(_(e_using_float_as_number));
      break;
   case VAR_FUNC:
   case VAR_PARTIAL:
      emsg(_(e_using_funcref_as_number));
      break;
   case VAR_STRING:
     if (varp->string != NULL)
        readLongNumber(varp->string, NULL, NULL, STR2NR_ALL, &n, NULL, 0, false, NULL);
     return n;
   case VAR_LIST:
      emsg(_(e_using_list_as_number));
      break;
   case VAR_BAG:
      emsg(_(e_using_dictionary_as_number));
      break;
   case VAR_BOOL:
   case VAR_SPECIAL:
      return varp->number == VVAL_TRUE ? 1 : 0;
   case VAR_JOB:
       emsg(_(e_using_job_as_number));
       break;
   case VAR_CHANNEL:
       emsg(_(e_using_channel_as_number));
       break;
   case VAR_BLOB:
       emsg(_(e_using_blob_as_number));
       break;
   case VAR_VOID:
       emsg(_(e_cannot_use_void_value));
       break;
   case VAR_UNKNOWN:
   case VAR_ANY:
       internal_error_no_abort((CS)"tv_get_number(UNKNOWN)");
       break;
   }
   if (!denote)      // useful for values that must be unsigned
      n = -1;
   else
      *denote = true;
   return n;
}

//Get the numeric value of a variable. If it is a String variable, use readLongNumber(). For 
//incompatible types, return 0. varGetNumberChk() is similar to tv_get_number(), but informs the
//caller of incompatible types: set *denote to true if "denote" is not NULL or return -1 otherwise.
Long
tv_get_number(Var* varp) {
   return varGetNumberChk(varp, null);   // return 0L on error
}

Long
varGetNumberChk(Var* varp, OUT Boole* denote) {
   return convertToBoolOrNumber(varp, OUT denote);
}

//Get the boolean value of "varp". This is like varGetNumberChk(),
Boole
tv_get_bool(Var* varp) {
   return convertToBoolOrNumber(varp, NULL);
}

private double
convertToDouble(Var* varp, OUT Boole* error) {
   switch (varp->tag) {
   case VAR_NUMBER:
       return (double)(varp->number);
   case VAR_FLOAT:
       return varp->floatt;
   case VAR_FUNC:
   case VAR_PARTIAL:
       emsg(_(e_using_funcref_as_float));
       break;
   case VAR_STRING:
       emsg(_(e_using_string_as_float));
       break;
   case VAR_LIST:
       emsg(_(e_using_list_as_float));
       break;
   case VAR_BAG:
       emsg(_(e_using_dictionary_as_float));
       break;
   case VAR_BOOL:
       emsg(_(e_using_boolean_value_as_float));
       break;
   case VAR_SPECIAL:
       emsg(_(e_using_special_value_as_float));
       break;
   case VAR_JOB:
       emsg(_(e_using_job_as_float));
       break;
   case VAR_CHANNEL:
       emsg(_(e_using_channel_as_float));
       break;
   case VAR_BLOB:
       emsg(_(e_using_blob_as_float));
       break;
   case VAR_VOID:
       emsg(_(e_cannot_use_void_value));
       break;
   case VAR_UNKNOWN:
   case VAR_ANY:
       internal_error_no_abort((CS)"tv_get_float(UNKNOWN)");
       break;
   }
   if (error)
      *error = true;
   return 0;
}

double
tv_get_float(Var* varp) {
    return convertToDouble(varp, NULL);
}

// Give an error and return FAIL unless "args[idx]" is unknown
int
check_for_unknown_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_UNKNOWN) {
      showErrFmtMsg(_(e_too_many_arguments), idx + 1);
      return FAIL;
   }
   return OK;
}

// Give an error and return FAIL unless "args[idx]" is a string.
int
check_for_string_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_STRING) {
      showErrFmtMsg(_(e_string_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Give an error and return FAIL unless "args[idx]" is a non-empty string.
int
check_for_nonempty_string_arg(Var* args, int idx) {
   if (check_for_string_arg(args, idx) == FAIL)
      return FAIL;
   if (args[idx].string == NULL || *args[idx].string == ZERO) {
      showErrFmtMsg(_(e_non_empty_string_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional string argument at 'idx'
int
check_for_opt_string_arg(Arr(Var) args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN || check_for_string_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a number.
int
check_for_number_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional number argument at 'idx'
int
check_for_opt_number_arg(Var* args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN || check_for_number_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a float or a number.
int
check_for_float_or_nr_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_FLOAT && args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_float_or_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a bool.
int
check_for_bool_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_BOOL
       && !(args[idx].tag == VAR_NUMBER
         && (args[idx].number == 0 || args[idx].number == 1))
   ){
      showErrFmtMsg(_(e_bool_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a bool or a number.
private int
checkVarsForBoolOrNumber(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_BOOL && args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_bool_or_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional bool argument at 'idx'. Return FAIL if the type is wrong.
int
check_for_opt_bool_arg(Var* args, int idx) {
   if (args[idx].tag == VAR_UNKNOWN)
      return OK;
   return check_for_bool_arg(args, idx);
}

//Check for an optional bool or number argument at 'idx'. Return FAIL if the type is wrong.
int
check_for_opt_bool_or_number_arg(Var* args, int idx) {
   if (args[idx].tag == VAR_UNKNOWN)
      return OK;
   return checkVarsForBoolOrNumber(args, idx);
}

//Give an error and return FAIL unless "args[idx]" is a blob.
int
check_for_blob_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Give an error and return FAIL unless "args[idx]" is a list.
int
confirmVarIsList(Var* arg, int idx) {
   if (arg[idx].tag != VAR_LIST) {
      showErrFmtMsg(_(e_list_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a non-NULL list.
int
confirmVarIsNonnullList(Var* args, int idx) {
   if (confirmVarIsList(args, idx) == FAIL)
      return FAIL;

   if (args[idx].list == NULL) {
      showErrFmtMsg(_(e_non_null_list_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional list argument at 'idx'
int
confirmVarIsOptionalList(Arr(Var) args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || confirmVarIsList(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a dict.
int
check_for_dict_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_BAG) {
      showErrFmtMsg(_(e_dict_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a non-NULL dict.
int
check_for_nonnull_dict_arg(Arr(Var) args, int idx) {
   if (check_for_dict_arg(args, idx) == FAIL)
      return FAIL;

   if (args[idx].bag == NULL) {
      showErrFmtMsg(_(e_non_null_dict_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Check for an optional dict argument at 'idx'
int
check_for_oself_arg(Arr(Var) args, int idx) {
    return (args[idx].tag == VAR_UNKNOWN
       || check_for_dict_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Check for an optional non-NULL dict argument at 'idx'
int
check_for_opt_nonnull_dict_arg(Var* args, int idx) {
    return (args[idx].tag == VAR_UNKNOWN
       || check_for_nonnull_dict_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a channel or a job.
int
check_for_chan_or_job_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_CHANNEL && args[idx].tag != VAR_JOB) {
      showErrFmtMsg(_(e_chan_or_job_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is an optional channel or a job.
int
check_for_opt_chan_or_job_arg(Var* args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_chan_or_job_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a job.
int
check_for_job_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_JOB) {
      showErrFmtMsg(_(e_job_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Check for an optional job argument at 'idx'.
int
check_for_opt_job_arg(Arr(Var) args, int idx) {
    return (args[idx].tag == VAR_UNKNOWN
       || check_for_job_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a string or a number.
int
check_for_string_or_number_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_string_or_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional string or number argument at 'idx'.
int
check_for_opt_string_or_number_arg(Var* args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_string_or_number_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a buffer number.
//Book number can be a number or a string.
int
check_for_buffer_arg(Var* args, int idx) {
   return check_for_string_or_number_arg(args, idx);
}

//Check for an optional buffer argument at 'idx'
int
check_for_opt_buffer_arg(Arr(Var) args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_buffer_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a line number.
//Line number can be a number or a string.
int
check_for_lnum_arg(Arr(Var) args, int idx) {
   return check_for_string_or_number_arg(args, idx);
}

//Check for an optional line number argument at 'idx'
int
check_for_opt_lnum_arg(Arr(Var) args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_lnum_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a string or a blob.
int
check_for_string_or_blob_arg(Var* args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_string_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a string or a list.
int
check_for_string_or_list_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_LIST) {
      showErrFmtMsg(_(e_string_or_list_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a string, a list or a blob.
private int
check_for_string_or_list_or_blob_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_LIST && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_string_list_tuple_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional string or list argument at 'idx'
int
check_for_opt_string_or_list_arg(Arr(Var) args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_string_or_list_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a string or a dict.
int
check_for_string_or_dict_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_BAG) {
   showErrFmtMsg(_(e_string_or_dict_required_for_argument_nr), idx + 1);
   return FAIL;
    }
    return OK;
}

// Give an error and return FAIL unless "args[idx]" is a string or a number or a list.
int
check_for_string_or_number_or_list_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING
       && args[idx].tag != VAR_NUMBER
       && args[idx].tag != VAR_LIST) {
      showErrFmtMsg(_(e_string_number_or_list_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is an optional string or number or a list
int
check_for_opt_string_or_number_or_list_arg(Arr(Var) args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_string_or_number_or_list_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a string, a number, a list or a blob
int
check_for_repeat_func_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING
       && args[idx].tag != VAR_NUMBER
       && args[idx].tag != VAR_LIST
       && args[idx].tag != VAR_BLOB)
    {
   showErrFmtMsg(_(e_repeatable_type_required_for_argument_nr), idx + 1);
   return FAIL;
    }
    return OK;
}

//Give an error and return FAIL unless "args[idx]" is a string, a list or a dict.
int
check_for_string_list_or_dict_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_STRING
       && args[idx].tag != VAR_LIST
       && args[idx].tag != VAR_BAG
   ) {
      showErrFmtMsg(_(e_string_list_or_dict_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Give an error and return FAIL unless "args[idx]" is a string or a function reference.
int
check_for_string_or_func_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_PARTIAL
       && args[idx].tag != VAR_FUNC
       && args[idx].tag != VAR_STRING)
   {
      showErrFmtMsg(_(e_string_or_function_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list or a blob.
int
check_for_list_or_blob_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_LIST && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_list_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list.
int
check_for_list_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_LIST) {
      showErrFmtMsg(_(e_list_or_tuple_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list or a blob.
int
check_for_list_or_or_blob_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_LIST && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_list_or_tuple_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list or a dict
int
check_for_list_or_dict_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_LIST
       && args[idx].tag != VAR_BAG)
    {
      showErrFmtMsg(_(e_list_or_tuple_or_dict_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list or dict or a blob.
int
check_for_list_or_dict_or_blob_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_LIST
       && args[idx].tag != VAR_BAG
       && args[idx].tag != VAR_BLOB)
    {
   showErrFmtMsg(_(e_list_dict_or_blob_required_for_argument_nr), idx + 1);
   return FAIL;
    }
    return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list, a dict, a blob or a string
int
check_for_list_dict_blob_or_string_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_LIST
       && args[idx].tag != VAR_BAG
       && args[idx].tag != VAR_BLOB
       && args[idx].tag != VAR_STRING)
    {
      showErrFmtMsg(_(e_list_tuple_dict_blob_or_string_required_for_argument_nr), idx + 1);
      return FAIL;
    }
    return OK;
}

//Give an error and return FAIL unless "args[idx]" is an optional buffer number or a dict.
int
check_for_opt_buffer_or_dict_arg(Arr(Var) args, int idx) {
   if (args[idx].tag != VAR_UNKNOWN
       && args[idx].tag != VAR_STRING
       && args[idx].tag != VAR_NUMBER
       && args[idx].tag != VAR_BAG)
   {
      showErrFmtMsg(_(e_string_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}


//Get the string value of a variable.
//If it is a Number variable, the number is converted into a string.
//tv_get_string() uses a single, static buffer.  YOU CAN ONLY USE IT ONCE!
//tv_get_string_buf() uses a given buffer.
//If the String variable has never been set, return an empty string.
//Never returns NULL;
//convertVarToStringSingleUse() and convertVarToString() are similar, but return NULL on error.
CS
tv_get_string(Var* varp) {
   static Byte mybuf[NUMBUFLEN];
   return tv_get_string_buf(varp, mybuf);
}

//Like tv_get_string() but don't allow number to string conversion for Vim9.
CS
tv_get_string_strict(Var* varp) {
   static Byte mybuf[NUMBUFLEN];
   CS res =  convertVarToString_strict(varp, mybuf, false);

   return res != NULL ? res : (CS)"";
}

CS
tv_get_string_buf(Var* varp, CS buf) {
    CS res = convertVarToString(varp, buf);
    return res ? res : S"";
}

//Careful: This uses a single, static buffer. YOU CAN ONLY USE IT ONCE!
CS
convertVarToStringSingleUse(Var* varp) {
   static Byte mybuf[NUMBUFLEN];
   return convertVarToString(varp, mybuf);
}

CS
convertVarToString(Var* varp, CS buf) {
   return convertVarToString_strict(varp, buf, false);
}

CS
convertVarToString_strict(Var* varp, CS buf, int strict) {
   switch (varp->tag) {
   case VAR_NUMBER:
      if (strict) {
         emsg(_(e_using_number_as_string));
         break;
      }
      eeSnprintf(buf, NUMBUFLEN, "%ld", (Long)varp->number);
      return buf;
   case VAR_FUNC:
   case VAR_PARTIAL:
      emsg(_(e_using_funcref_as_string));
      break;
   case VAR_LIST:
      emsg(_(e_using_list_as_string));
      break;
   case VAR_BAG:
      emsg(_(e_using_dictionary_as_string));
      break;
   case VAR_FLOAT:
      if (strict) {
         emsg(_(e_using_float_as_string));
         break;
      }
      eeSnprintf(buf, NUMBUFLEN, "%g", varp->floatt);
      return buf;
   case VAR_STRING:
      if (varp->string)
         return varp->string;
      return S"";
   case VAR_BOOL:
   case VAR_SPECIAL:
      STRCPY(buf, get_var_special_name(varp->number));
      return buf;
   case VAR_BLOB:
      emsg(_(e_using_blob_as_string));
      break;
   case VAR_JOB:
      job_to_string_buf(OUT buf, varp);
      return buf;
   case VAR_CHANNEL:
      channel_to_string_buf(OUT buf, varp);
      return buf;
   case VAR_VOID:
      emsg(_(e_cannot_use_void_value));
      break;
   case VAR_UNKNOWN:
   case VAR_ANY:
      showErrFmtMsg(_(e_using_invalid_value_as_string_str), vartype_name(varp->tag));
      break;
   }
   return NULL;
}

// Turn a var into a string. Similar to tv_get_string_buf() but uses string() on Bag, List, etc.
CS
tv_stringify(Var* varp, CS buf) {
   if (varp->tag == VAR_LIST
       || varp->tag == VAR_BAG
       || varp->tag == VAR_BLOB
       || varp->tag == VAR_FUNC
       || varp->tag == VAR_PARTIAL
       || varp->tag == VAR_FLOAT
   ) {
      Var tmp;

      initVarToNull(OUT &tmp);
      f_string(varp, &tmp);
      tv_get_string_buf(&tmp, buf);
      clearVar(varp);
      *varp = tmp;
      return tmp.string;
   }
   return tv_get_string_buf(varp, buf);
}

//Return true if typeval "tv" and its value are set to be locked (immutable).
//Also give an error message, using "name" or _("name") when use_gettext is true.
int
tv_check_lock(Var* tv, Text name, Boole use_gettext) {
   int   lock = 0;

   switch (tv->tag) {
   case VAR_BLOB:
      if (tv->blob)
         lock = tv->blob->lock;
      break;
   case VAR_LIST:
      if (tv->list != NULL)
         lock = tv->list->lock;
      break;
   case VAR_BAG:
      if (tv->bag)
         lock = tv->bag->lock;
      break;
   default:
       break;
    }
    return value_check_lock(tv->lock, name, use_gettext)
         || (lock != 0 && value_check_lock(lock, name, use_gettext));
}

//Copy the values from Var "from" to Var "to". When needed allocates string or increases reference 
//count. Does not make a copy of a list, blob or dict but copies the reference!
//It is OK for "from" and "to" to point to the same item. This is used to make a copy later.
void
copy_tv(OUT Var* to, Var* from) {
   to->tag = from->tag;
   to->lock = 0;
   switch (from->tag) {
   case VAR_NUMBER:
   case VAR_BOOL:
   case VAR_SPECIAL:
       to->number = from->number;
       break;
   case VAR_FLOAT:
       to->floatt = from->floatt;
       break;
   case VAR_JOB:
       to->job = from->job;
       if (to->job != NULL)
      ++to->job->jv_refcount;
       break;
   case VAR_CHANNEL:
       to->channel = from->channel;
       if (to->channel != NULL)
      ++to->channel->refcount;
       break;
   case VAR_STRING:
   case VAR_FUNC:
      if (from->string == NULL)
         to->string = NULL;
      else {
         to->string = copyStr(from->string);
         if (from->tag == VAR_FUNC)
            func_ref(to->string);
      }
      break;
   case VAR_PARTIAL:
      if (from->partial == NULL)
         to->partial = NULL;
      else {
         to->partial = from->partial;
         ++to->partial->refcount;
      }
      break;
   case VAR_BLOB:
      if (from->blob == NULL)
         to->blob = NULL;
      else {
         to->blob = from->blob;
         ++to->blob->refcount;
      }
      break;
   case VAR_LIST:
      if (!from->list)
         to->list = NULL;
      else {
         to->list = from->list;
         ++to->list->refcount;
      }
      break;
   case VAR_BAG:
      if (from->bag == NULL)
         to->bag = NULL;
      else {
         to->bag = from->bag;
         ++to->bag->refcount;
      }
      break;
   case VAR_VOID:
      emsg(_(e_cannot_use_void_value));
      break;
   case VAR_UNKNOWN:
   case VAR_ANY:
       internal_error_no_abort((CS)"copy_tv(UNKNOWN)");
       break;
   }
}

//Compare "tv1" and "tv2". Put the result in "tv1".  Caller should clear "tv2".
int
typval_compare(
   Var   *tv1,   // first operand
   Var   *tv2,   // second operand
   ExprType   type,   // operator
   Boole ignoreCase
) {
   Long   n1, n2;
   int      res = 0;
   int      type_is = type == EXPR_IS || type == EXPR_ISNOT;

   if (type_is && tv1->tag != tv2->tag) {
      //For "is" a different type always means false, for "isnot" it means true.
      n1 = (type == EXPR_ISNOT);
   } ei (((tv1->tag == VAR_SPECIAL && tv1->number == VVAL_NULL)
      || (tv2->tag == VAR_SPECIAL && tv2->number == VVAL_NULL))
       && tv1->tag != tv2->tag
       && (type == EXPR_EQUAL || type == EXPR_NEQUAL)
   ) {
      n1 = typval_compare_null(tv1, tv2);
      if (n1 == MAYBE) {
         clearVar(tv1);
         return FAIL;
      }
      if (type == EXPR_NEQUAL)
         n1 = !n1;
   } ei (tv1->tag == VAR_BLOB || tv2->tag == VAR_BLOB) {
      if (typval_compare_blob(tv1, tv2, type, &res) == FAIL) {
         clearVar(tv1);
         return FAIL;
      }
      n1 = res;
   } ei (tv1->tag == VAR_LIST || tv2->tag == VAR_LIST) {
      if (typval_compare_list(tv1, tv2, type, ignoreCase, &res) == FAIL) {
         clearVar(tv1);
         return FAIL;
      }
      n1 = res;
   } ei (tv1->tag == VAR_BAG || tv2->tag == VAR_BAG) {
      if (typval_compare_dict(tv1, tv2, type, ignoreCase, &res) == FAIL) {
          clearVar(tv1);
          return FAIL;
      }
      n1 = res;
   } ei (tv1->tag == VAR_FUNC || tv2->tag == VAR_FUNC
      || tv1->tag == VAR_PARTIAL || tv2->tag == VAR_PARTIAL
   ) {
      if (typval_compare_func(tv1, tv2, type, ignoreCase, &res) == FAIL) {
          clearVar(tv1);
          return FAIL;
      }
      n1 = res;
   }

   // If one of the two variables is a float, compare as a float.
   // When using "=~" or "!~", always compare as string.
   ei ((tv1->tag == VAR_FLOAT || tv2->tag == VAR_FLOAT)
       && type != EXPR_MATCH && type != EXPR_NOMATCH
   ){
   double f1, f2;
   Boole error = false;

   f1 = convertToDouble(tv1, OUT &error);
   if (!error)
       f2 = convertToDouble(tv2, OUT &error);
   if (error) {
       clearVar(tv1);
       return FAIL;
   }
   n1 = false;
   switch (type) {
   case EXPR_IS:
   case EXPR_EQUAL:    n1 = (f1 == f2); break;
   case EXPR_ISNOT:
   case EXPR_NEQUAL:   n1 = (f1 != f2); break;
   case EXPR_GREATER:  n1 = (f1 > f2); break;
   case EXPR_GEQUAL:   n1 = (f1 >= f2); break;
   case EXPR_SMALLER:  n1 = (f1 < f2); break;
   case EXPR_SEQUAL:   n1 = (f1 <= f2); break;
   case EXPR_UNKNOWN:
   case EXPR_MATCH:
   default:  break;  // avoid gcc warning
   }
   }

   // If one of the two variables is a number, compare as a number.
   // When using "=~" or "!~", always compare as string.
   ei ((tv1->tag == VAR_NUMBER || tv2->tag == VAR_NUMBER)
       && type != EXPR_MATCH && type != EXPR_NOMATCH
   ){
      Boole error = false;

      n1 = varGetNumberChk(tv1, OUT &error);
      if (!error)
          n2 = varGetNumberChk(tv2, OUT &error);
      if (error) {
          clearVar(tv1);
          return FAIL;
      }
      switch (type) {
      case EXPR_IS:
      case EXPR_EQUAL:    n1 = (n1 == n2); break;
      case EXPR_ISNOT:
      case EXPR_NEQUAL:   n1 = (n1 != n2); break;
      case EXPR_GREATER:  n1 = (n1 > n2); break;
      case EXPR_GEQUAL:   n1 = (n1 >= n2); break;
      case EXPR_SMALLER:  n1 = (n1 < n2); break;
      case EXPR_SEQUAL:   n1 = (n1 <= n2); break;
      case EXPR_UNKNOWN:
      case EXPR_MATCH:
      default:  break;  // avoid gcc warning
      }
   } ei (tv1->tag == tv2->tag
       && (tv1->tag == VAR_CHANNEL || tv1->tag == VAR_JOB)
       && (type == EXPR_NEQUAL || type == EXPR_EQUAL)
   ){
      if (tv1->tag == VAR_CHANNEL)
         n1 = tv1->channel == tv2->channel;
      else
         n1 = tv1->job == tv2->job;
      if (type == EXPR_NEQUAL)
         n1 = !n1;
   } else {
      if (typval_compare_string(tv1, tv2, type, ignoreCase, &res) == FAIL) {
         clearVar(tv1);
         return FAIL;
      }
      n1 = res;
   }
   clearVar(tv1);
   tv1->tag = VAR_NUMBER;
   tv1->number = n1;

   return OK;
}

//Compare "tv1" to "tv2" as lists according to "type" and "ic".
//Put the result, false or true, in "res".
//Return FAIL and give an error message when the comparison can't be done.
int
typval_compare_list(
   Var    *tv1,
   Var    *tv2,
   ExprType  type,
   Boole       ic,
   int       *res
) {
   int       val = 0;

   if (type == EXPR_IS || type == EXPR_ISNOT) {
      val = (tv1->tag == tv2->tag && tv1->list == tv2->list);
   if (type == EXPR_ISNOT)
       val = !val;
   } ei (tv1->tag != tv2->tag || (type != EXPR_EQUAL && type != EXPR_NEQUAL)) {
      if (tv1->tag != tv2->tag)
          emsg(_(e_can_only_compare_list_with_list));
      else
          emsg(_(e_invalid_operation_for_list));
      return FAIL;
   } else {
      val = list_equal(tv1->list, tv2->list, ic);
      if (type == EXPR_NEQUAL)
          val = !val;
   }
   *res = val;
   return OK;
}


//Compare v:null with another type.  Return true if the value is NULL.
int
typval_compare_null(Var *tv1, Var *tv2) {
   if ((tv1->tag == VAR_SPECIAL && tv1->number == VVAL_NULL)
       || (tv2->tag == VAR_SPECIAL && tv2->number == VVAL_NULL)
   ) {
      Var   *tv = tv1->tag == VAR_SPECIAL ? tv2 : tv1;

      switch (tv->tag) {
      case VAR_BLOB: return tv->blob == NULL;
      case VAR_CHANNEL: return tv->channel == NULL;
      // TODO: null_class handling
      case VAR_BAG: return tv->bag == NULL;
      case VAR_FUNC: return tv->string == NULL;
      case VAR_JOB: return tv->job == NULL;
      case VAR_LIST: return tv->list == NULL;
      case VAR_PARTIAL: return tv->partial == NULL;
      case VAR_STRING: return tv->string == NULL;

      case VAR_NUMBER: 
         return tv->number == 0;
      case VAR_FLOAT: 
         return tv->floatt == 0.0;
      default: break;
      }
   }
   // although comparing null with number, float or bool is not very useful
   // we won't give an error
    return false;
}

//Compare "tv1" to "tv2" as blobs according to "type".
//Put the result, false or true, in "res".
//Return FAIL and give an error message when the comparison can't be done.
int
typval_compare_blob(Var* tv1, Var* tv2, ExprType type, int* res) {
   int       val = 0;

   if (type == EXPR_IS || type == EXPR_ISNOT) {
      val = (tv1->tag == tv2->tag
            && tv1->blob == tv2->blob);
      if (type == EXPR_ISNOT)
          val = !val;
   } ei (tv1->tag != tv2->tag || (type != EXPR_EQUAL && type != EXPR_NEQUAL)) {
      if (tv1->tag != tv2->tag)
         emsg(_(e_can_only_compare_blob_with_blob));
      else
         emsg(_(e_invalid_operation_for_blob));
      return FAIL;
   } else {
      val = blob_equal(tv1->blob, tv2->blob);
      if (type == EXPR_NEQUAL)
         val = !val;
   }
   *res = val;
   return OK;
}

//Compare "tv1" to "tv2" as dictionaries according to "type" and "ic".
//Put the result, false or true, in "res".
//Return FAIL and give an error message when the comparison can't be done.
int
typval_compare_dict(
   Var* tv1,
   Var* tv2,
   ExprType  type,
   int ic,
   int* res
) {
   int val;
   if (type == EXPR_IS || type == EXPR_ISNOT) {
      val = (tv1->tag == tv2->tag && tv1->bag == tv2->bag);
      if (type == EXPR_ISNOT)
          val = !val;
   } ei (tv1->tag != tv2->tag || (type != EXPR_EQUAL && type != EXPR_NEQUAL)) {
      if (tv1->tag != tv2->tag)
          emsg(_(e_can_only_compare_dictionary_with_dictionary));
      else
          emsg(_(e_invalid_operation_for_dictionary));
      return FAIL;
   } else {
      val = bagEqual(tv1->bag, tv2->bag, ic);
      if (type == EXPR_NEQUAL)
          val = !val;
   }
   *res = val;
   return OK;
}

//Compare "tv1" to "tv2" as funcrefs according to "type" and "ic".
//Put the result, false or true, in "res".
//Return FAIL and give an error message when the comparison can't be done.
int
typval_compare_func(
   Var* tv1,
   Var* tv2,
   ExprType type,
   int ic,
   int* res
) {
   int val = 0;

   if (type != EXPR_EQUAL && type != EXPR_NEQUAL && type != EXPR_IS && type != EXPR_ISNOT) {
      emsg(_(e_invalid_operation_for_funcrefs));
      return FAIL;
   }
   if ((tv1->tag == VAR_PARTIAL && !tv1->partial)
          || (tv2->tag == VAR_PARTIAL && !tv2->partial))
      //When both partials are NULL, then they are equal. Otherwise they are not equal.
      val = (tv1->partial == tv2->partial);
   ei (type == EXPR_IS || type == EXPR_ISNOT) {
      if (tv1->tag == VAR_FUNC && tv2->tag == VAR_FUNC)
         // strings are considered the same if their value is the same
         val = tv_equal(tv1, tv2, ic);
      ei (tv1->tag == VAR_PARTIAL && tv2->tag == VAR_PARTIAL)
         val = (tv1->partial == tv2->partial);
      else
         val = false;
   } else
      val = tv_equal(tv1, tv2, ic);
   if (type == EXPR_NEQUAL || type == EXPR_ISNOT)
      val = !val;
   *res = val;
   return OK;
}

//Compare "tv1" to "tv2" as strings according to "type" and "ic".
//Put the result, false or true, in "res".
//Return FAIL and give an error message when the comparison can't be done.
int
typval_compare_string(
   Var* tv1,
   Var* tv2,
   ExprType type,
   Boole ignoreCase,
   int* res
) {
   int i = 0;
   int val = false;
   Byte buf1[NUMBUFLEN], buf2[NUMBUFLEN];

   CS s1 = tv_get_string_buf(tv1, buf1);
   CS s2 = tv_get_string_buf(tv2, buf2);
   if (type != EXPR_MATCH && type != EXPR_NOMATCH)
      i = ignoreCase ? caseInsensitiveCompareMaxCol(s1, s2) : STRCMP(s1, s2);
   switch (type) {
   case EXPR_IS: 
      // FALLTHROUGH
   case EXPR_EQUAL:    
      val = (i == 0); break;
   case EXPR_ISNOT:    
      // FALLTHROUGH
   case EXPR_NEQUAL:   
      val = (i != 0); break;
   case EXPR_GREATER:  
      val = (i > 0); break;
   case EXPR_GEQUAL:   val = (i >= 0); break;
   case EXPR_SMALLER:  val = (i < 0); break;
   case EXPR_SEQUAL:   val = (i <= 0); break;

   case EXPR_MATCH:
   case EXPR_NOMATCH:
      val = pattern_match(s2, s1, ignoreCase);
      if (type == EXPR_NOMATCH)
          val = !val;
      break;

   default:  break;  // avoid gcc warning
   }
   *res = val;
   return OK;
}
//Convert any type to a string, never give an error.
//When "quotes" is true add quotes to a string. Return an allocated string.
CS
typval_tostring(Var *arg, int quotes) {
   CS tofree;
   Byte numbuf[NUMBUFLEN];
   CS ret = NULL;

   if (!arg)
      return copyStr((CS)"(does not exist)");
   if (!quotes && arg->tag == VAR_STRING) {
      ret = copyStr(arg->string == NULL ? E : arg->string);
   } else {
      ret = tv2string(arg, &tofree, numbuf, 0);
      // Make a copy if we have a value but it's not in allocated memory.
      if (ret && !tofree)
         ret = copyStr(ret);
   }
   return ret;
}

//Return true if internal var is locked: Either that value is locked itself
//or it refers to a List or Bag that is locked.
int
tv_islocked(Var *tv) {
    return (tv->lock & VAR_LOCKED)
      || (tv->tag == VAR_LIST
         && tv->list != NULL
         && (tv->list->lock & VAR_LOCKED))
      || (tv->tag == VAR_BAG
         && tv->bag != NULL
         && (tv->bag->lock & VAR_LOCKED));
}

private int
func_equal(Var *tv1, Var *tv2, int ic) {      // ignore case
   int      i;
   // empty and NULL function name considered the same
   CS s1 = tv1->tag == VAR_FUNC ? tv1->string : partial_name(tv1->partial);
   if (s1 && *s1 == ZERO)
      s1 = NULL;
   CS s2 = tv2->tag == VAR_FUNC ? tv2->string : partial_name(tv2->partial);
   if (s2 && *s2 == ZERO)
      s2 = NULL;
   if (!s1 || !s2) {
      if (s1 != s2)
          return false;
   } ei (STRCMP(s1, s2) != 0)
      return false;

   // empty dict and NULL dict is different
   Bag* d1 = tv1->tag == VAR_FUNC ? NULL : tv1->partial->self;
   Bag* d2 = tv2->tag == VAR_FUNC ? NULL : tv2->partial->self;
   if (!d1 || !d2) {
      if (d1 != d2)
         return false;
   } ei (!bagEqual(d1, d2, ic))
      return false;

   // empty list and no list considered the same
   int a1 = tv1->tag == VAR_FUNC ? 0 : tv1->partial->argc;
   int a2 = tv2->tag == VAR_FUNC ? 0 : tv2->partial->argc;
   if (a1 != a2)
      return false;
   for (i = 0; i < a1; ++i) {
      if (!tv_equal(tv1->partial->argv + i, tv2->partial->argv + i, ic))
         return false;
   } 

   return true;
}

//Return true if "tv1" and "tv2" have the same value.
//Compares the items just like "==" would compare them, but strings and
//numbers are different.  Floats and numbers are also different.
int
tv_equal(Var* tv1, Var* tv2, int ic) {      // ignore case
   Byte buf1[NUMBUFLEN], buf2[NUMBUFLEN];
   CS s1;
   CS s2;
   static int  recursive_cnt = 0;       // catch recursive loops
   int      r;
   static int   tv_equal_recurse_limit;

   //Catch lists and dicts that have an endless loop by limiting recursiveness to a limit. We 
   //guess they are equal then. A fixed limit has the problem of still taking an awful long time.
   //Reduce the limit every time running into it. That should work fine for deeply linked 
   //structures that are not recursively linked and catch recursiveness quickly.
   if (recursive_cnt == 0)
   tv_equal_recurse_limit = 1000;
   if (recursive_cnt >= tv_equal_recurse_limit) {
      --tv_equal_recurse_limit;
      return true;
   }

   // For VAR_FUNC and VAR_PARTIAL compare the function name, bound dict and arguments.
   if ((tv1->tag == VAR_FUNC
      || (tv1->tag == VAR_PARTIAL && tv1->partial != NULL))
       && (tv2->tag == VAR_FUNC
      || (tv2->tag == VAR_PARTIAL && tv2->partial != NULL)))
    {
      ++recursive_cnt;
      r = func_equal(tv1, tv2, ic);
      --recursive_cnt;
      return r;
   }

   if (tv1->tag != tv2->tag
          && ((tv1->tag != VAR_BOOL && tv1->tag != VAR_SPECIAL)
            || (tv2->tag != VAR_BOOL && tv2->tag != VAR_SPECIAL)))
      return false;

   switch (tv1->tag) {
   case VAR_LIST:
      ++recursive_cnt;
      r = list_equal(tv1->list, tv2->list, ic);
      --recursive_cnt;
      return r;

   case VAR_BAG:
      ++recursive_cnt;
      r = bagEqual(tv1->bag, tv2->bag, ic);
      --recursive_cnt;
      return r;

   case VAR_BLOB:
      return blob_equal(tv1->blob, tv2->blob);

   case VAR_NUMBER:
   case VAR_BOOL:
   case VAR_SPECIAL:
      return tv1->number == tv2->number;

   case VAR_STRING:
      s1 = tv_get_string_buf(tv1, buf1);
      s2 = tv_get_string_buf(tv2, buf2);
      return ((ic ? caseInsensitiveCompareMaxCol(s1, s2) : STRCMP(s1, s2)) == 0);

   case VAR_FLOAT:
      return tv1->floatt == tv2->floatt;
   case VAR_JOB:
      return tv1->job == tv2->job;
   case VAR_CHANNEL:
      return tv1->channel == tv2->channel;
   case VAR_PARTIAL:
      return tv1->partial == tv2->partial;

   case VAR_FUNC:
      return tv1->string == tv2->string;

   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      break;
   }

   // VAR_UNKNOWN can be the result of a invalid expression, let's say it
   // does not equal anything, not even itself.
   return false;
}

//Get an option value.
//"arg" points to the '&' or '+' before the option name.
//"arg" is advanced to character after the option name.
//Return OK or FAIL.
int
eval_option(
   Byte** arg,
   Var* returnVar,   // when NULL, only check if option exists
   int evaluate
) {
   int ret = OK;

   // Isolate the option name and find its value.
   int scope;
   CS option_end = find_option_end(OUT arg, OUT &scope);
   if (!option_end) {
      if (returnVar)
         showErrFmtMsg(_(e_option_name_missing_str), *arg);
      return FAIL;
   }

   if (!evaluate) {
      *arg = option_end;
      return OK;
   }

   int c = *option_end;
   *option_end = ZERO;
   OptionValue optVal = optGetValue(null, *arg, scope);

   if (returnVar) {
      returnVar->lock = 0;
      if (optVal.tag == OPTION_BOOLE) {
         returnVar->tag = VAR_NUMBER;
         returnVar->number = optVal.boole;
      } ei (optVal.tag == OPTION_NUM) {
         returnVar->tag = VAR_NUMBER;
         returnVar->number = optVal.num;
      } else {            // string option
          returnVar->tag = VAR_STRING;
          returnVar->string = optVal.string;
      }
   } 

   *option_end = c;          // put back for error messages
   *arg = option_end;

   return ret;
}

//Allocate a variable for a number constant. Also deals with "0z" for blob. Return OK or FAIL.
int
eval_number(CS* arg, Var* returnVar, int evaluate, int want_string) {
   int get_float = false;

   // We accept a float when the format matches
   // "[0-9]\+\.[0-9]\+\([eE][+-]\?[0-9]\+\)\?".  This is very
   // strict to avoid backwards compatibility problems.
   // The leading digit can be omitted.
   // Don't look for a float after the "." operator, so that ":let vers = 1.2.3" doesn't fail.
   CS p;
   if (**arg == '.')
      p = *arg;
   else {
      p = *arg + 1;
      for (;;) {
         if (*p == '\'')
            ++p;
         if (!eeIsDigit(*p))
            break;
         p = skipdigits(p);
      }
   }
   if (!want_string && p[0] == '.' && eeIsDigit(p[1])) {
      get_float = true;
      p = skipdigits(p + 2);
      if (*p == 'e' || *p == 'E') {
         ++p;
         if (*p == '-' || *p == '+')
            ++p;
         if (!eeIsDigit(*p))
            get_float = false;
         else
            p = skipdigits(p + 1);
      }
      if (ASCII_ISALPHA(*p) || *p == '.')
         get_float = false;
   }
   if (get_float) {
      double   f;
      *arg += string2float(*arg, OUT &f, true);
      if (evaluate) {
          returnVar->tag = VAR_FLOAT;
          returnVar->floatt = f;
      }
   } ei (**arg == '0' && ((*arg)[1] == 'z' || (*arg)[1] == 'Z')) {
      CS  bp;
      Blob* blob = NULL;

      // Blob constant: 0z0123456789abcdef
      if (evaluate)
         blob = blob_alloc();
      for (bp = *arg + 2; eeIsXDigit(bp[0]); bp += 2) {
         if (!eeIsXDigit(bp[1])) {
            if (blob != NULL) {
                emsg(_(e_blob_literal_should_have_an_even_number_of_hex_characters));
                ga_clear(&blob->c);
                EE_CLEAR(blob);
            }
            return FAIL;
         }
         if (blob != NULL)
            ga_append(&blob->c, (hex2nr(*bp) << 4) + hex2nr(*(bp+1)));
         if (bp[2] == '.' && eeIsXDigit(bp[3]))
            ++bp;
      }
      if (blob)
          returnVar_blob_set(returnVar, blob);
      *arg = bp;
   } else {

      // decimal or hex number
      int len;
      Long n;
      readLongNumber(
         *arg, NULL, OUT &len, STR2NR_ALL + STR2NR_QUOTE, OUT &n, NULL, 0, true, NULL
      );
      if (len == 0) {
         if (evaluate)
            showErrFmtMsg(_(e_invalid_expression_str), *arg);
         return FAIL;
      }
      *arg += len;
      if (evaluate) {
         returnVar->tag = VAR_NUMBER;
         returnVar->number = n;
      }
   }
   return OK;
}

//Return a string with the string representation of a variable.
//If the memory is allocated "tofree" is set to it, otherwise NULL.
//"numbuf" is used for a number.
//Puts quotes around strings, so that they can be parsed back by eval(). May return NULL.
CS
tv2string(
   Var* tv,
   Byte** tofree,
   CS numbuf,
   int copyID)
{
   return echo_string_core(tv, tofree, numbuf, copyID, false, true, false);
}

//Get the value of an environment variable.
//"arg" is pointing to the '$'.  It is advanced to after the name.
//If the environment variable was not set, silently assume it is empty.
//Return FAIL if the name is invalid.
int
eval_env_var(OUT CS* arg, Var* returnVar, int evaluate) {
   CS string = NULL;
   int cc;
   int mustfree = false;

   ++*arg;
   CS name = *arg;
   int len = readEnvNameAndGetItsLen(arg);
   if (evaluate) {
      if (len == 0)
         return FAIL; // invalid empty name

      cc = name[len];
      name[len] = ZERO;
      // first try eeglGetEnv(), fast for normal environment vars
      string = eeglGetEnv(name);
      if (string && *string != ZERO) {
         if (!mustfree)
            string = copyStr(string);
      } else {
         if (mustfree)
            eeglFree(string);

         // next try expanding things like $EEGL and ${HOME}
         string = doExpandEnvInMultiplePaths(name - 1);
         if (string != NULL && *string == '$')
            EE_CLEAR(string);
      }
      name[len] = cc;

      returnVar->tag = VAR_STRING;
      returnVar->string = string;
      returnVar->lock = 0;
   }

   return OK;
}

//Get the lnum from the first argument.
//Also accepts ".", "$", etc., but that only works for the current buffer. Return -1 on error.
LineNr
tv_get_lnum(Arr(Var) argvars) {
   int      anyEmsgG_before = anyEmsgG;

   LineNr lnum = (LineNr)varGetNumberChk(&argvars[0], NULL);
   if (lnum <= 0 && anyEmsgG_before == anyEmsgG && argvars[0].tag != VAR_NUMBER) {
      int fnum;
      // no valid number, try using arg like line()
      Pos* fp = var2fpos(&argvars[0], true, &fnum, false);
      if (fp)
          lnum = fp->lnum;
   }
   return lnum;
}

//Get the lnum from the first argument. Also accept "$", then "book" is used. Return 0 on error.
LineNr
daGetLnumFromBookOrVar(Var* argvars, Book* book) {
   if (argvars[0].tag == VAR_STRING
          && argvars[0].string != NULL
          && argvars[0].string[0] == '$'
          && argvars[0].string[1] == ZERO
          && book
   )
      return book->mem.lineCount;
   return (LineNr)varGetNumberChk(&argvars[0], NULL);
}

//Get book by number or pattern.
Book *
daGetBook(Var* tv, Boole curtab_only) {
   CS name = tv->string;

   if (tv->tag == VAR_NUMBER)
      return bookFindFileByBookNr((int)tv->number);
   if (tv->tag != VAR_STRING)
      return NULL;
   if (!name || *name == ZERO)
      return curBook;
   if (name[0] == '$' && name[1] == ZERO)
      return lastBook;

   Book* book = bookFindByName(name, curtab_only);

   // If not found, try expanding the name, like done for bufexists().
   if (!book)
      book = findBook(tv);

   return book;
}

//Like daGetBook() but give an error message is the type is wrong.
Book *
daGetBookFromArg(Var* tv) {
   ++emsg_off;
   Book* book = daGetBook(tv, false);
   --emsg_off;
   if (!book
       && tv->tag != VAR_NUMBER
       && tv->tag != VAR_STRING)
      // issue errmsg for type error
      (void)tv_get_number(tv);
   return book;
}


//Check if "type1" and "type2" are exactly the same.
//"flags" can have ETYPE_ARG_UNKNOWN, which means that an unknown argument
//type in "type1" is accepted.
int
equal_type(TypeSpec *type1, TypeSpec *type2, int flags) {
   int i;

   if (type1 == NULL || type2 == NULL)
      return false;
   if (type1->tag != type2->tag)
      return false;
   switch (type1->tag) {
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_SPECIAL:
   case VAR_BOOL:
   case VAR_NUMBER:
   case VAR_FLOAT:
   case VAR_STRING:
   case VAR_BLOB:
   case VAR_JOB:
   case VAR_CHANNEL:
      break;  // not composite is always OK
   case VAR_LIST:
   case VAR_BAG:
      return equal_type(type1->member, type2->member, flags);
   case VAR_FUNC:
   case VAR_PARTIAL:
      if (!equal_type(type1->member, type2->member, flags) || type1->argCount != type2->argCount)
         return false;
      if (type1->argCount < 0 || type1->args == NULL || type2->args == NULL)
         return true;
      for (i = 0; i < type1->argCount; ++i) {
         if ((flags & ETYPE_ARG_UNKNOWN) == 0
            && !equal_type(type1->args[i], type2->args[i], flags))
             return false;
      } 
      return true;
    }
    return true;
}

ExprType
get_compare_type(CS p, int* len, int* type_is) {
   ExprType   type = EXPR_UNKNOWN;
   int      i;

   switch (p[0]) {
   case '=':
      if (p[1] == '=')
         type = EXPR_EQUAL;
      ei (p[1] == '~')
         type = EXPR_MATCH;
      break;
   case '!': 
      if (p[1] == '=')
         type = EXPR_NEQUAL;
      ei (p[1] == '~')
         type = EXPR_NOMATCH;
      break;
   case '>':
      if (p[1] != '=') {
         type = EXPR_GREATER;
         *len = 1;
      } else
         type = EXPR_GEQUAL;
      break;
   case '<':   
      if (p[1] != '=') {
         type = EXPR_SMALLER;
         *len = 1;
      } else
         type = EXPR_SEQUAL;
      break;
   case 'i':   
      if (p[1] == 's') {
         // "is" and "isnot"; but not a prefix of a name
         if (p[2] == 'n' && p[3] == 'o' && p[4] == 't')
            *len = 5;
         i = p[*len];
         if (!SAFE_isalnum(i) && i != '_') {
            type = *len == 2 ? EXPR_IS : EXPR_ISNOT;
            *type_is = true;
         }
      }
      break;
   }
   return type;
}

//Return true when "tv" is not falsy: non-zero, non-empty string, non-empty
//list, etc.  Mostly like what JavaScript does, except that empty list and
//empty dictionary are false.
int
tv2bool(Var* tv) {
   switch (tv->tag) {
   case VAR_NUMBER:
      return tv->number != 0;
   case VAR_FLOAT:
      return tv->floatt != 0.0;
   case VAR_PARTIAL:
      return tv->partial != NULL;
   case VAR_FUNC:
   case VAR_STRING:
      return tv->string && *tv->string != ZERO;
   case VAR_LIST:
      return tv->list && tv->list->len > 0;
   case VAR_BAG:
      return tv->bag && tv->bag->hashTable.count > 0;
   case VAR_BOOL:
   case VAR_SPECIAL:
      return tv->number == VVAL_TRUE ? true : false;
   case VAR_JOB:
      return tv->job != NULL;
   case VAR_CHANNEL:
      return tv->channel != NULL;
   case VAR_BLOB:
      return tv->blob && tv->blob->c.len > 0;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      break;
   }
   return false;
}

//}}}
//{{{Bags

// List head for garbage collection. Although there can be a reference loop
// from partial to dict to partial, we don't need to keep track of the partial,
// since it will get freed when the dict is unused and gets freed.
private Bag* first_dict = NULL;

// Allocate an empty header for a dictionary. Caller should take care of the reference count.
Bag *
allocBag(void) {
   Bag* d = ALLOC_CLEAR_ONE(Bag);
   if (!d)
      return NULL;

   // Add the dict to the list of dicts for garbage collection.
   if (first_dict)
      first_dict->dv_used_prev = d;
   d->dv_used_next = first_dict;
   d->dv_used_prev = NULL;
   first_dict = d;

   hash_init(&d->hashTable);
   d->lock = 0;
   d->scope = 0;
   d->refcount = 0;
   d->copyId = 0;
   return d;
}

//allocBag() with an ID for alloc_fail().
Bag *
allocBag_id(AllocId id UNUSED) {
   if (alloc_fail_id == id && alloc_does_fail(sizeof(List)))
      return NULL;
   return allocBag();
}

Bag *
allocBag_lock(int lock) {
   Bag *d = allocBag();
   d->lock = lock;
   return d;
}

// Allocate an empty dict for a return value. Return OK or FAIL.
void
allocReturnDict(Var* returnVar) {
   Bag* b = allocBag_lock(0);
   returnVar_dict_set(returnVar, b);
}

// Set a dictionary as the return value
void
returnVar_dict_set(Var* returnVar, Bag *d) {
   returnVar->tag = VAR_BAG;
   returnVar->bag = d;
   if (d)
      ++d->refcount;
}

//Return the character "str[index]" where "index" is the character index,
//including composing characters.
//If "index" is out of range NULL is returned.
CS
char_from_string(CS str, Long index) {
   Unt       nbyte = 0;
   Long       nchar = index;

   if (!str)
      return NULL;
   Unt slen = STRLEN(str);

   // Do the same as for a list: a negative index counts from the end.
   // Optimization to check the first byte to be below 0x80 (and no composing
   // character follows) makes this a lot faster.
   if (index < 0) {
      int   clen = 0;

      for (nbyte = 0; nbyte < slen; ++clen) {
         if (str[nbyte] < 0x80 && str[nbyte + 1] < 0x80)
            ++nbyte;
         else
            nbyte += utfCharLen(str + nbyte);
      }
      nchar = clen + index;
      if (nchar < 0)
         // unlike list: index out of range results in empty string
         return NULL;
   }

   for (nbyte = 0; nchar > 0 && nbyte < slen; --nchar) {
      if (str[nbyte] < 0x80 && str[nbyte + 1] < 0x80)
         ++nbyte;
      else
         nbyte += utfCharLen(str + nbyte);
   }
   if (nbyte >= slen)
      return NULL;
   return copySubstr(str + nbyte, utfCharLen(str + nbyte));
}

Text
textOfDi(DictItem* di) {
   return (Text){.c = di->key, .len = di->len};
}

Text
textOfDi16(DictItem16* di) {
   return (Text){.c = di->key, .len = di->len};
}

Text
textOfItem(EeSetItem* si) {
   return (Text){.c = si->hi_key, .len = si->len};
}

// Add item "item" to Bag "b". Return FAIL when wrong bag or when key already exists.
int
bagAdd(Bag* b, DictItem* item) {
   if (dictWrongFuncName(b, &item->c, textOfDi(item)))
      return FAIL;
   return hash_add(&b->hashTable, textOfDi(item), S"add to dictionary");
}

// Add a number or special entry to a Bag. FAIL when out of memory or when key exists
private int
bagAddNumber_special(Bag* b, CS key, Long nr, VarTag vartype) {
   DictItem* item = dictitem_alloc(mbText(key));
   item->c.tag = vartype;
   item->c.number = nr;
   if (bagAdd(b, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   return OK;
}

// Add a number entry to a Bag. Return FAIL when out of memory or when key already exists.
int
bagAddNumber(Bag* d, CS key, Long nr) {
   return bagAddNumber_special(d, key, nr, VAR_NUMBER);
}

// Add a special entry to a Bag. FAIL when out of memory or when key already exists.
int
bagAdd_bool(Bag* d, CS key, Long nr) {
   return bagAddNumber_special(d, key, nr, VAR_BOOL);
}

// Add a string entry to Bag. Return FAIL when out of memory or when key already exists.
int
bagAddString(Bag* d, CS key, CS str) {
   return bagAddString_len(d, key, str, -1);
}

//Add a string entry to Bag. "str" will be copied to allocated memory.
//When "len" is -1 use the whole string, otherwise only this many bytes.
//Return FAIL when out of memory and when key already exists.
int
bagAddString_len(Bag *d, CS key, CS str, int len) {
   CS val = NULL;
   DictItem* item = dictitem_alloc(mbText(key));
   item->c.tag = VAR_STRING;
   if (str) {
      if (len == -1)
         val = copyStr(str);
      else
         val = copySubstr(str, len);
   }
   item->c.string = val;
   if (bagAdd(d, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   return OK;
}

// Add a list entry to dictionary "d". Return FAIL when out of memory and when key already exists.
int
bagAddList(Bag* d, CS key, List* list) {
   DictItem* item = dictitem_alloc(mbText(key));
   item->c.tag = VAR_LIST;
   item->c.list = list;
   ++list->refcount;
   if (bagAdd(d, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   return OK;
}

// Add a Var entry to dictionary "d". Return FAIL when out of memory and when key already exists.
int
bagAddVar(Bag* b, CS key, Var* tv) {
   DictItem* item = dictitem_alloc(mbText(key));
   copy_tv(OUT &item->c, tv);
   if (bagAdd(b, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   return OK;
}

// Add a callback to dictionary "d". Return FAIL when out of memory and when key already exists.
int
bagAddCallback(Bag* d, CS key, Callback* cb) {
   DictItem* item = dictitem_alloc(mbText(key));
   putCallback(OUT &item->c, cb);
   if (bagAdd(d, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   return OK;
}

// Add a function entry to dictionary "d". FAIL when out of memory or when key already exists.
int
bagAddFn(Bag* d, CS key, UserFunc* fp) {
   DictItem* item = dictitem_alloc(mbText(key));
   item->c.tag = VAR_FUNC;
   item->c.string = copySubstr(fp->uf_name, fp->uf_namelen);
   if (bagAdd(d, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   func_ref(item->c.string);
   return OK;
}

// Initialize "iter" for iterating over dictionary items with bagIterateNext().
// If "var" is not a Bag or an empty Bag then there will be nothing to iterate over, no error 
// is given. NOTE: The dictionary must not change until iterating is finished!
void
bagIterateStart(Var* var, DictIterator* iter) {
   if (var->tag != VAR_BAG || var->bag == NULL)
      iter->dit_todo = 0;
   else {
      Bag   *d = var->bag;

      iter->dit_todo = d->hashTable.count;
      iter->dit_hi = d->hashTable.array;
   }
}

//Iterate over the items referred to by "iter". It should be initialized with bagIterateStart().
//Return a pointer to the key. "*tv_result" is set to point to the value for that key.
//If there are no more items, NULL is returned.
CS
bagIterateNext(DictIterator* iter, Var** tv_result) {
   if (iter->dit_todo == 0)
      return NULL;

   while (HASHITEM_EMPTY(iter->dit_hi))
      ++iter->dit_hi;

   DictItem* di = HI2DI(iter->dit_hi);
   CS result = di->key;
   *tv_result = &di->c;

   --iter->dit_todo;
   ++iter->dit_hi;
   return result;
}

// Add a dict entry to dictionary "d".
// Return FAIL when out of memory and when key already exists.
int
bagAddBag(Bag* d, CS key, Bag* dict) {
   DictItem* item = dictitem_alloc(mbText(key));
   item->c.tag = VAR_BAG;
   item->c.bag = dict;
   ++dict->refcount;
   if (bagAdd(d, item) == FAIL) {
      dictitem_free(item);
      return FAIL;
   }
   return OK;
}

// Get the number of items in a Dictionary.
Ulong
bagSize(Bag* b) {
   return b ? (Long)b->hashTable.count : 0L;
}

// Find item "key[len]" in Dictionary "d". If "len" is negative use strlen(key). NULL when not found
DictItem*
bagFind(Bag* b, Text const key) {
#define AKEYLEN 200
   Byte   buf[AKEYLEN];
   CS akey;
   CS tofree = NULL;

   if (!b)
      return NULL;
   if (key.len >= AKEYLEN) {
      tofree = akey = copySubstr(key.c, key.len);
   } else {
      // Avoid a malloc/free by using buf[].
      copySubstrToAllocation(buf, key);
      akey = buf;
   }

   EeSetItem* hi = hash_find(&b->hashTable, (Text){.c = akey, .len = key.len});
   eeglFree(tofree);
   if (HASHITEM_EMPTY(hi))
      return NULL;
   return HI2DI(hi);
}

// Return true if "key" is present in Dictionary "d".
int
bagHasKey(Bag* b, Text key) {
   return bagFind(b, key) != NULL;
}

// Get a Var item from a dictionary and copy it into "returnVar".
// Return FAIL if the entry doesn't exist or out of memory.
int
bagGetVar(Bag *d, Text key, Var* returnVar) {
   DictItem* di = bagFind(d, key);
   if (!di)
      return FAIL;
   copy_tv(OUT returnVar, &di->c);
   return OK;
}

//Get a string item from a dictionary. When "save" is true allocate memory for it. When false 
//a shared buffer is used, can only be used once! Return NULL if the entry doesn't exist or out 
//of memory.
CS
bagGetString(Bag* d, Text key, Boole save) {
   DictItem* di = bagFind(d, key);
   if (!di)
      return NULL;
   CS s = tv_get_string(&di->c);
   if (save && s)
      s = copyStr(s);
   return s;
}

//Get a number item from a dictionary. Return 0 if the entry doesn't exist.
Long
bagGetNumber(Bag *d, Text key) {
    return bagGetNumber_def(d, key, 0);
}

//Get a number item from a dictionary. Return "def" if the entry doesn't exist.
Long
bagGetNumber_def(Bag* b, Text const key, int def) {
   DictItem* di = bagFind(b, key);
   if (!di)
      return def;
   return tv_get_number(&di->c);
}

//Get a number item from a dictionary. Return 0 if the entry doesn't exist.
//Give an error if the entry is not a number.
Long
bagGetNumber_check(Bag* b, Text const key) {
   DictItem* di = bagFind(b, key);
   if (!di)
      return 0;
   if (di->c.tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&di->c));
      return 0;
   }
   return tv_get_number(&di->c);
}

//Get a bool item (number or true/false) from a dictionary. Return "def" if the entry doesn't exist.
Boole
bagGetBool(Bag *d, Text key, Boole def) {
   DictItem* di = bagFind(d, key);
   if (!di)
      return def;
   return tv_get_bool(&di->c);
}

//Return an allocated string with the string representation of a Dictionary. May return NULL.
CS
bagToString(Var* tv, int copyID, int restore_copyID) {
   ArrayList   ga;
   Boole first = true;
   Byte numbuf[NUMBUFLEN];
   EeSetItem   *hi;
   CS s;
   Bag* b;

   if ((b = tv->bag) == NULL)
      return NULL;
   ga_init2(&ga, sizeof(Byte), 80);
   ga_append(&ga, '{');

   int todo = (int)b->hashTable.count;
   FOR_ALL_HASHTAB_ITEMS(&b->hashTable, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;

         if (first)
            first = false;
         else
            ga_concat(&ga, S", ");

         CS tofree = string_quote(hi->hi_key, false);
         if (tofree) {
            ga_concat(&ga, tofree);
            eeglFree(tofree);
         }
         ga_concat(&ga, S": ");
         s = echo_string_core(&HI2DI(hi)->c, &tofree, numbuf, copyID, false, restore_copyID, true);
         if (s)
            ga_concat(&ga, s);
         eeglFree(tofree);
         if (s == NULL || did_echo_string_emsg)
            break;
         line_breakcheck();

      }
    }
   if (todo > 0) {
      eeglFree(ga.c);
      return NULL;
   }

   ga_append(&ga, '}');
   ga_append(&ga, ZERO);
   return (CS)ga.c;
}

//Advance over a literal key, including "-".  If the first character is not a
// literal key character then "key" is returned.
private CS
skip_literal_key(CS key) {
   CS p;

   for (p = key; ASCII_ISALNUM(*p) || *p == '_' || *p == '-'; ++p)
      {}
   return p;
}

// Get the key for #{key: val} into "tv" and advance "arg". Return FAIL when there is no valid key.
private int
get_literal_key_tv(Arr(CS) arg, Var *tv) {
   CS p = skip_literal_key(*arg);

   if (p == *arg)
      return FAIL;
   tv->tag = VAR_STRING;
   tv->string = copySubstr(*arg, p - *arg);

   *arg = p;
   return OK;
}

//Allocate a variable for a Dictionary and fill it from "*arg". "*arg" points to the opening brace.
//"literal" is true for #{key: val}
//Return OK or FAIL, or NOTDONE for {expr}.
int
bagEval(OUT CS* arg, Var* returnVar, EvalCtx *evalarg, int literal) {
   int evaluate = evalarg == NULL ? false : (evalarg->eval_flags & EVAL_EVALUATE);
   Bag   *d = NULL;
   Var   tvkey;
   Var   tv;
   DictItem   *item;
   CS curly_expr = skipwhite(*arg + 1);
   Byte buf[NUMBUFLEN];
   int had_comma;

   // First check if it's not a curly-braces expression: {expr}. Must do this without evaluating, 
   // otherwise a function may be called twice. Unfortunately this means we need to call eval1() 
   // twice for the first item. 
   // "{}" is an empty Dictionary. "#{abc}" is never a curly-braces expression.
   if (*curly_expr != '}'
          && !literal
          && eval1(&curly_expr, &tv, NULL) == OK
          && *skipwhite(curly_expr) == '}')
      return NOTDONE;

   if (evaluate) {
      d = allocBag();
   }
   tvkey.tag = VAR_UNKNOWN;
   tv.tag = VAR_UNKNOWN;
   CS keyStr;

   *arg = skipwhite_and_linebreak(*arg + 1, evalarg);
   while (**arg != '}' && **arg != ZERO) {
      if (literal) {
         if (get_literal_key_tv(arg, &tvkey) == FAIL)
            goto failret;
      } else {
         if (eval1(arg, &tvkey, evalarg) == FAIL)   // recursive!
            goto failret;
      }

      *arg = skipwhite(*arg);
      if (**arg != ':') {
         if (*skipwhite(*arg) == ':')
            showErrFmtMsg(_(e_no_white_space_allowed_before_str_str), ":", *arg);
         else
            showErrFmtMsg(_(e_missing_colon_in_dictionary_str), *arg);
         clearVar(&tvkey);
         goto failret;
      }
      if (evaluate) {
         if (tvkey.tag == VAR_FLOAT) {
            tvkey.string = typval_tostring(&tvkey, true);
            tvkey.tag = VAR_STRING;
         }
         keyStr = convertVarToString(&tvkey, buf);
         if (!keyStr) {
            //"key" is NULL when convertVarToString() gave an errmsg
            clearVar(&tvkey);
            goto failret;
         }
      }

      *arg = skipwhite_and_linebreak(*arg + 1, evalarg);
      if (eval1(arg, &tv, evalarg) == FAIL) {  // recursive!
         if (evaluate)
            clearVar(&tvkey);
         goto failret;
      }
      if (evaluate) {
         Text sli = mbText(keyStr);
         item = bagFind(d, sli);
         if (item) {
            showErrFmtMsg(_(e_duplicate_key_in_dictionary_str), keyStr);
            clearVar(&tvkey);
            clearVar(&tv);
            goto failret;
         }
         item = dictitem_alloc(sli);
         item->c = tv;
         item->c.lock = 0;
         if (bagAdd(d, item) == FAIL)
            dictitem_free(item);
      }
      clearVar(&tvkey);

      *arg = skipwhite(*arg);
      had_comma = **arg == ',';
      if (had_comma) {
         *arg = skipwhite_and_nl(*arg + 1);
      }

      // the "}" can be on the next line
      *arg = skipwhite_and_linebreak(*arg, evalarg);
      if (**arg == '}')
         break;
      if (!had_comma) {
         if (**arg == ',')
            showErrFmtMsg(_(e_no_white_space_allowed_before_str_str), ",", *arg);
         else
            showErrFmtMsg(_(e_missing_comma_in_dictionary_str), *arg);
         goto failret;
      }
   }

   if (**arg != '}') {
      if (evalarg)
         showErrFmtMsg(_(e_missing_dict_end_str), *arg);
failret:
      if (d)
         dict_free(d);
      return FAIL;
    }

   *arg = *arg + 1;
   if (evaluate)
      returnVar_dict_set(returnVar, d);

   return OK;
}

// Evaluate a literal dictionary: #{key: val, key: val} "*arg" points to the "#".
// On return, "*arg" points to the character after the Bag.
// Return OK or FAIL.  Returns NOTDONE for {expr}.
int
bagEvalLiteral(OUT CS* arg, Var* returnVar, EvalCtx *evalarg) {
   int      ret = OK;

   if ((*arg)[1] == '{') {
      ++*arg;
      ret = bagEval(OUT arg, returnVar, evalarg, true);
   } else
      ret = NOTDONE;

   return ret;
}

// Make a copy of a Dictionary item.
private DictItem *
dictitem_copy(DictItem* org) {
   Unt   len = STRLEN(org->key);
   DictItem* di = alloc(offsetof(DictItem, key) + len + 1);

   MEMMOVE(di->key, org->key, len + 1);
   di->flags = DI_FLAGS_ALLOC;
   copy_tv(OUT &di->c, &org->c);
   return di;
}

// Go over all entries in "d2" and add them to "d1".
// When "action" is "error" then a duplicate key is an error.
// When "action" is "force" then a duplicate key is overwritten.
// When "action" is "move" then move items instead of copying.
// Otherwise duplicate keys are ignored ("action" is "keep").
// "funcName" is used for reporting where an error occurred.
void
bagExtend(Bag* d1, Bag* d2, CS action) {
   DictItem* di1;
   Text arg_errmsg = tConst("extend() argument");

   if (check_hashtab_frozen(&d1->hashTable, S"extend"))
      return;

   if (*action == 'm') {
      if (check_hashtab_frozen(&d2->hashTable, S"extend"))
         return;
      hash_lock(&d2->hashTable);  // don't rehash on hash_remove()
   }

   int todo = (int)d2->hashTable.count;
   EeSetItem *hi2;
   FOR_ALL_HASHTAB_ITEMS(&d2->hashTable, hi2, todo) {
      if (!HASHITEM_EMPTY(hi2)) {
         --todo;
         Text t2 = textOfItem(hi2);
         di1 = bagFind(d1, t2);
         // Check the key to be valid when adding to any scope.
         if (d1->scope != 0 && !valid_varname(t2, true))
            break;

         if (!di1) {
            if (*action == 'm') {
               // Cheap way to move a dict item from "d2" to "d1".
               // If bagAdd() fails then "d2" won't be empty.
               di1 = HI2DI(hi2);
               if (bagAdd(d1, di1) == OK)
                  hash_remove(&d2->hashTable, hi2, S"extend");
            } else {
               di1 = dictitem_copy(HI2DI(hi2));
               if (di1 != NULL && bagAdd(d1, di1) == FAIL)
                  dictitem_free(di1);
            }
         } ei (*action == 'e') {
            showErrFmtMsg(_(e_key_already_exists_str), hi2->hi_key);
            break;
         } ei (*action == 'f' && HI2DI(hi2) != di1) {
            if (value_check_lock(di1->c.lock, arg_errmsg, true)
                  || var_check_ro(di1->flags, arg_errmsg, true))
               break;
            // Disallow replacing a builtin function.
            if (dictWrongFuncName(d1, &HI2DI(hi2)->c, textOfItem(hi2)))
               break;
            clearVar(&di1->c);
            copy_tv(OUT &di1->c, &HI2DI(hi2)->c);
         }
      }
   }

   if (*action == 'm')
      hash_unlock(&d2->hashTable);
}

// Return the dictitem that an entry in a hashtable points to.
DictItem *
bagLookup(EeSetItem* hi) {
   return HI2DI(hi);
}

// Return true when two bags have exactly the same key/values.
int
bagEqual(Bag* d1, Bag* d2, int ic) {      // ignore case for strings
   EeSetItem   *hi;
   DictItem   *item2;
   int todo;

   if (d1 == d2)
      return true;
   if (bagSize(d1) != bagSize(d2))
      return false;
   if (bagSize(d1) == 0)
      // empty and NULL dicts are considered equal
      return true;
   if (d1 == NULL || d2 == NULL)
      return false;

   todo = (int)d1->hashTable.count;
   FOR_ALL_HASHTAB_ITEMS(&d1->hashTable, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
          item2 = bagFind(d2, textOfItem(hi));
          if (item2 == NULL)
         return false;
          if (!tv_equal(&HI2DI(hi)->c, &item2->c, ic))
         return false;
          --todo;
      }
   }
   return true;
}

// Count the number of times item "needle" occurs in Bag "d". Case is ignored if "ic" is true.
long
bagCount(Bag* d, Var* needle, int ic) {
   if (!d)
      return 0;

   int todo = (int)d->hashTable.count;
   EeSetItem   *hi;
   long   n = 0;
   FOR_ALL_HASHTAB_ITEMS(&d->hashTable, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         if (tv_equal(&HI2DI(hi)->c, needle, ic))
            ++n;
      }
   }

   return n;
}

// extend() a Bag. Append Bag argvars[1] to Bag argvars[0] and return the
// resulting Bag in "returnVar".  "is_new" is true for extendnew().
void
bagExtend_func(Var* argvars, CS arg_errmsg, int is_new, Var* returnVar) {
   int   i;

   Bag* d1 = argvars[0].bag;
   if (!d1) {
      emsg(_(e_cannot_extend_null_dict));
      return;
   }
   Bag* d2 = argvars[1].bag;
   if (!d2)
      return;

   if (!is_new && value_check_lock(d1->lock, mbText(arg_errmsg), true))
      return;

   if (is_new) {
      d1 = dict_copy(d1, false, true, get_copyID());
      if (d1 == NULL)
          return;
   }

   // Check the third argument.
   CS action;
   if (argvars[2].tag != VAR_UNKNOWN) {
      static CS (av[]) = {S"keep", S"force", S"error"};

      action = convertVarToStringSingleUse(&argvars[2]);
      if (action == NULL) {
          if (is_new)
         bagUnref(d1);
          return;
      }
      for (i = 0; i < 3; ++i) {
         if (STRCMP(action, av[i]) == 0)
            break;
      } 
      if (i == 3) {
         if (is_new)
            bagUnref(d1);
         showErrFmtMsg(_(e_invalid_argument_str), action);
         return;
      }
   } else
      action = (CS)"force";

   bagExtend(d1, d2, action);

   if (is_new) {
      returnVar->tag = VAR_BAG;
      returnVar->bag = d1;
      returnVar->lock = false;
   } else
      copy_tv(OUT returnVar, &argvars[0]);
}

// Implementation of map(), filter(), foreach() for a Bag.  Apply "expr" to
// every item in Bag "d" and return the result in "returnVar".
void
bagFilterMap(
   Bag* d,
   FilterMap filtermap,
   CS arg_errmsg,
   Var* expr,
   Var* returnVar
) {
   Bag   *d_ret = NULL;
   EeSet   *ht;
   EeSetItem   *hi;
   DictItem   *di;
   int      rem;
   Var   newtv;

   if (filtermap == FILTERMAP_MAPNEW) {
      returnVar->tag = VAR_BAG;
      returnVar->bag = NULL;
   }
   if (!d || (filtermap == FILTERMAP_FILTER && value_check_lock(d->lock, mbText(arg_errmsg), true))
   )
      return;

   if (filtermap == FILTERMAP_MAPNEW) {
      allocReturnDict(returnVar);
      d_ret = returnVar->bag;
   }

   int prev_lock = d->lock;
   if (d->lock == 0)
      d->lock = VAR_LOCKED;
   ht = &d->hashTable;
   hash_lock(ht);
   int todo = (int)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         di = HI2DI(hi);
         Text errMsg = mbText(arg_errmsg);
         if (filtermap == FILTERMAP_MAP
                && (value_check_lock(di->c.lock, errMsg, true)
                  || var_check_ro(di->flags, errMsg, true)))
            break;
         set_EeglVar_string(VV_KEY, di->key, -1);
         int r = filter_map_one(&di->c, expr, filtermap, &newtv, &rem);
         clearVar(get_EeglVar_tv(VV_KEY));
         if (r == FAIL || anyEmsgG) {
            clearVar(&newtv);
            break;
         }
         if (filtermap == FILTERMAP_MAP) {
            // map(): replace the dict item value
            clearVar(&di->c);
            newtv.lock = 0;
            di->c = newtv;
         } ei (filtermap == FILTERMAP_MAPNEW) {
            // mapnew(): add the item value to the new dict
            r = bagAddVar(d_ret, di->key, &newtv);
            clearVar(&newtv);
            if (r == FAIL)
                break;
          } ei (filtermap == FILTERMAP_FILTER && rem) {
            Text errMsg = mbText(arg_errmsg);
            // filter(false): remove the item from the dict
            if (var_check_fixed(di->flags, errMsg, true) || var_check_ro(di->flags, errMsg, true))
                break;
            dictitem_remove(d, di, S"filter");
         }
      }
   }
   hash_unlock(ht);
   d->lock = prev_lock;
}

// "remove({dict})" function
void
bagRemove(Arr(Var) argvars, Var* returnVar, CS arg_errmsg) {
   if (argvars[2].tag != VAR_UNKNOWN) {
      showErrFmtMsg(_(e_too_many_arguments_for_function_str), "remove()");
      return;
   }

   Bag* b = argvars[0].bag;
   if (!b || value_check_lock(b->lock, mbText(arg_errmsg), true))
      return;

   CS key = convertVarToStringSingleUse(&argvars[1]);
   if (!key)
      return;

   DictItem* di = bagFind(b, text(key));
   if (!di) {
      showErrFmtMsg(_(e_key_not_present_in_dictionary_str), key);
      return;
   }
   Text errMsg = mbText(arg_errmsg);
   if (var_check_fixed(di->flags, errMsg, true) || var_check_ro(di->flags, errMsg, true))
      return;

   *returnVar = di->c;
   initVarToNull(OUT &di->c);
   dictitem_remove(b, di, S"remove()");
}

typedef enum {
   DICT2LIST_KEYS,
   DICT2LIST_VALUES,
   DICT2LIST_ITEMS,
} dict2List;

// Turn a dict into a list.
private void
bagToList(Arr(Var) argvars, Var* returnVar, dict2List what) {
   List   *l2;
   DictItem   *di;
   EeSetItem   *hi;
   ListItem   *li;

   allocReturnList(returnVar);

   if ((what == DICT2LIST_ITEMS
         ? check_for_string_list_or_dict_arg(argvars, 0)
         : check_for_dict_arg(argvars, 0)) == FAIL
   )
      return;

   Bag* d = argvars[0].bag;
   if (d == NULL)
      // NULL dict behaves like an empty dict
      return;

   int todo = (int)d->hashTable.count;
   FOR_ALL_HASHTAB_ITEMS(&d->hashTable, hi, todo) {
      if (HASHITEM_EMPTY(hi)) {
         continue;
      }
      --todo;
      di = HI2DI(hi);

      li = listitem_alloc();
      list_append(returnVar->list, li);

      if (what == DICT2LIST_KEYS) {
         // keys()
         li->c.tag = VAR_STRING;
         li->c.lock = 0;
         li->c.string = copyStr(di->key);
      } ei (what == DICT2LIST_VALUES) {
         // values()
         copy_tv(OUT &li->c, &di->c);
      } else {
         // items()
         l2 = list_alloc();
         li->c.tag = VAR_LIST;
         li->c.lock = 0;
         li->c.list = l2;
         if (l2 == NULL)
             break;
         ++l2->refcount;

         if (list_append_string(l2, di->key, -1) == FAIL
            || list_append_tv(l2, &di->c) == FAIL)
             break;
      }
   }
}

// "items(dict)" function
void
f_items(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_STRING)
      string2items(argvars, returnVar);
   ei (argvars[0].tag == VAR_LIST)
      list2items(argvars, returnVar);
   else
      bagToList(argvars, returnVar, DICT2LIST_ITEMS);
}

void
f_keys(Arr(Var) argvars, Var* returnVar) {
   bagToList(argvars, returnVar, DICT2LIST_KEYS);
}

void
f_values(Arr(Var) argvars, Var* returnVar) {
   bagToList(argvars, returnVar, DICT2LIST_VALUES);
}

// Make each item in the bag readonly (not the value of the item).
void
bagSetItemsRo(Bag* di) {
   int      todo = (int)di->hashTable.count;
   EeSetItem   *hi;

   // Set readonly
   FOR_ALL_HASHTAB_ITEMS(&di->hashTable, hi, todo) {
      if (HASHITEM_EMPTY(hi))
         continue;
      --todo;
      HI2DI(hi)->flags |= DI_FLAGS_RO | DI_FLAGS_FIX;
   }
}

void
f_has_key(Arr(Var) argvars, Var* returnVar) {
   if (check_for_dict_arg(argvars, 0) == FAIL || argvars[0].bag == NULL)
      return;

   returnVar->number = bagHasKey(argvars[0].bag, mbText(tv_get_string(&argvars[1])));
}

//Return the slice "str[first : last]" using character indexes.  Composing
//characters are included. "exclusive" is true for slice().
//Return NULL when the result is empty.
CS
string_slice(CS str, Long first, Long last, int exclusive) {
   if (!str)
      return NULL;
   Unt slen = STRLEN(str);
   Long start_byte = char_idx2byte(str, slen, first);
   if (start_byte < 0)
      start_byte = 0; // first index very negative: use zero
   Long   end_byte;
   if ((last == -1 && !exclusive) || last == (Long)LONG_MAX)
      end_byte = (long)slen;
   else {
      end_byte = char_idx2byte(str, slen, last);
      if (!exclusive && end_byte >= 0 && end_byte < (long)slen)
          // end index is inclusive
          end_byte += utfCharLen(str + end_byte);
   }

   if (start_byte >= (long)slen || end_byte <= start_byte)
      return NULL;
   return copySubstr(str + start_byte, end_byte - start_byte);
}

// Unreference a Dictionary: decrement the reference count and free it when it becomes zero.
void
bagUnref(Bag *d) {
   if (d && --d->refcount <= 0)
      dict_free(d);
}


// Go through the list of dicts and free items without the copyID. true if anything was freed.
int
dict_free_nonref(int copyID) {
   Boole did_free = false;

   for (Bag* dd = first_dict; dd != NULL; dd = dd->dv_used_next) {
      if ((dd->copyId & COPYID_MASK) != (copyID & COPYID_MASK)) {
         // Free the Dictionary and ordinary items it contains, but don't
         // recurse into Lists and Dictionaries, they will be in the list
         // of dicts or list of lists.
         dict_free_contents(dd);
         did_free = true;
      }
   } 
   return did_free;
}

// Clear hashtab "ht" and dict items it contains.
// If "ht" is not freed then you should call hash_init() next!
void
hashtab_free_contents(EeSet* ht) {
   EeSetItem   *hi;
   DictItem   *di;

   if (check_hashtab_frozen(ht, S"clear dict"))
      return;

   // Lock the hashtab, we don't want it to resize while freeing items.
   hash_lock(ht);
   int todo = (int)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
          // Remove the item before deleting it, just in case there is
          // something recursive causing trouble.
          di = HI2DI(hi);
          hash_remove(ht, hi, S"clear dict");
          dictitem_free(di);
          --todo;
      }
   }

   // The hashtab is still locked, it has to be re-initialized anyway.
   hash_clear(ht);
}

private void
dict_free_dict(Bag *d) {
   // Remove the dict from the list of dicts for garbage collection.
   if (d->dv_used_prev == NULL)
      first_dict = d->dv_used_next;
   else
      d->dv_used_prev->dv_used_next = d->dv_used_next;
   if (d->dv_used_next != NULL)
      d->dv_used_next->dv_used_prev = d->dv_used_prev;
   eeglFree(d);
}


private void
dict_free(Bag* d) {
  if (!in_free_unref_items) {
      dict_free_contents(d);
      dict_free_dict(d);
   }
}

void
dict_free_items(int copyID) {
   Bag* dd_next;

   //Return the slice "str[first : last]" using character indexes.  Composing
   for (Bag* dd = first_dict; dd != NULL; dd = dd_next) {
      dd_next = dd->dv_used_next;
      if ((dd->copyId & COPYID_MASK) != (copyID & COPYID_MASK))
          dict_free_dict(dd);
   }
}

// Free a Dictionary, including all non-container items it contains. Ignore the reference count.
void
dict_free_contents(Bag *d) {
   hashtab_free_contents(&d->hashTable);
   free_type(d->ty);
   d->ty = NULL;
}

// Allocate a Dictionary item. The "key" is copied to the new item.
// Note that the type and value of the item "c" still needs to be initialized after this!
DictItem *
dictitem_alloc(Text value) {
   DictItem* di = alloc(offsetof(DictItem, key) + value.len + 1);
   di->len = value.len;
   MEMMOVE(di->key, value.c, value.len + 1);
   di->flags = DI_FLAGS_ALLOC;
   di->c.lock = 0;
   di->c.tag = VAR_UNKNOWN;
   return di;
}

//Remove item "item" from Bag "bag" and free it.
//"command" is used for the error message when the hashtab if frozen.
void
dictitem_remove(Bag* bag, DictItem* item, CS command) {
   EeSetItem* hi = hash_find(&bag->hashTable, (Text){.c = item->key, .len = item->len});
   if (HASHITEM_EMPTY(hi))
      internal_error(S"dictitem_remove()");
   else
      hash_remove(&bag->hashTable, hi, command);
   dictitem_free(item);
}

// Free a dict item.  Also clears the value.
void
dictitem_free(DictItem *item) {
   clearVar(&item->c);
   if (item->flags & DI_FLAGS_ALLOC)
      eeglFree(item);
}

// Make a copy of dict "d".  Shallow if "deep" is false. The refcount of the new dict is set to 1.
// See item_copy() for "top" and "copyID". Return NULL when out of memory.
Bag *
dict_copy(Bag* orig, int deep, int top, int copyID) {
   if (!orig)
      return NULL;

   Bag* copy = allocBag();
      
   DictItem   *di;

   if (copyID != 0) {
      orig->copyId = copyID;
      orig->dv_copydict = copy;
   }
   copy->ty = (orig->ty == NULL || top || deep) ? null : alloc_type(orig->ty);

   int todo = (int)orig->hashTable.count;
   for (EeSetItem* hi = orig->hashTable.array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
          --todo;

         di = dictitem_alloc(textOfItem(hi));
         if (deep) {
            if (item_copy(&HI2DI(hi)->c, &di->c, deep, false, copyID) == FAIL) {
                eeglFree(di);
                break;
            }
         } else
            copy_tv(OUT &di->c, &HI2DI(hi)->c);
         if (bagAdd(copy, di) == FAIL) {
            dictitem_free(di);
            break;
         }
      }
   }

   ++copy->refcount;
   if (todo > 0) {
      bagUnref(copy);
      copy = NULL;
   }

   return copy;
}

// Check for adding a function to g: or or l:.
// If the name is wrong give an error message and return true.
int
dictWrongFuncName(Bag* b, Var* tv, Text name) {
   return (b == get_globvar_dict() || &b->hashTable == get_funccal_local_ht())
       && (tv->tag == VAR_FUNC || tv->tag == VAR_PARTIAL)
       && var_wrong_func_name(name, true);
}

//}}}
//{{{floating-point numerics

#define USING_FLOAT_STUFF

// Convert the string "text" to a floating point number.
// This uses strtod().  setlocale(LC_NUMERIC, "C") has been used to make sure
// this always uses a decimal point.
// Return the length of the text that was consumed.
int
string2float(CS text, OUT double* value, Boole skip_quotes) {
   CS s = text;
   double f;

   // MS-Portals does not deal with "inf" and "nan" properly.
   if (STRNICMP(text, "inf", 3) == 0) {
      *value = INFINITY;
      return 3;
    }
   if (STRNICMP(text, "-inf", 4) == 0) {
      *value = -INFINITY;
      return 4;
   }
   if (STRNICMP(text, "nan", 3) == 0) {
      *value = NAN;
      return 3;
   }
   if (skip_quotes && firstOccurrence((CS)s, '\'') != NULL) {
      Byte buf[100];
      int  quotes = 0;

      copySubstrToAllocation(buf, (Text){s, 99});
      for (CS p = buf; ; p = skipdigits(p)) {
         // remove single quotes between digits, not in the exponent
         if (*p == '\'') {
            ++quotes;
            MEMMOVE(p, p + 1, STRLEN(p));
         }
         if (!eeIsDigit(*p))
            break;
      }
      s = buf;
      f = STRTOD(s, &s);
      *value = f;
      return (int)((CS)s - buf) + quotes;
   }

   f = STRTOD(s, &s);
   *value = f;
   return (int)((CS)s - text);
}

// Get the float value of "argvars[0]" into "f".
// Return FAIL when the argument is not a Number or Float.
private int
get_float_arg(Arr(Var) argvars, OUT double* f) {
   if (argvars[0].tag == VAR_FLOAT) {
      *f = argvars[0].floatt;
      return OK;
   }
   if (argvars[0].tag == VAR_NUMBER) {
      *f = (double)argvars[0].number;
      return OK;
   }
   emsg(_(e_number_or_float_required));
   return FAIL;
}

// "abs(expr)" function
void
f_abs(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_FLOAT) {
      returnVar->tag = VAR_FLOAT;
      returnVar->floatt = fabs(argvars[0].floatt);
   } else {
      Boole error = false;
      Long n = varGetNumberChk(&argvars[0], OUT &error);
      if (error)
         returnVar->number = -1;
      ei (n > 0)
         returnVar->number = n;
      else
         returnVar->number = -n;
    }
}

void
f_acos(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = acos(f);
   else
      returnVar->floatt = 0.0;
}

void
f_asin(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = asin(f);
   else
      returnVar->floatt = 0.0;
}

void
f_atan(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;
   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = atan(f);
   else
      returnVar->floatt = 0.0;
}

void
f_atan2(Arr(Var) argvars, Var* returnVar) {
   double   fx = 0.0, fy = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &fx) == OK && get_float_arg(argvars + 1, OUT &fy) == OK)
      returnVar->floatt = atan2(fx, fy);
   else
      returnVar->floatt = 0.0;
}

// "ceil({float})" function
void
f_ceil(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = ceil(f);
   else
      returnVar->floatt = 0.0;
}

void
f_cos(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = cos(f);
   else
      returnVar->floatt = 0.0;
}

void
f_cosh(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = cosh(f);
    else
   returnVar->floatt = 0.0;
}

void
f_exp(Arr(Var) argvars, Var* returnVar) {
    double   f = 0.0;
   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = exp(f);
   else
      returnVar->floatt = 0.0;
}

// "float2nr({float})" function
void
f_float2nr(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   if (get_float_arg(argvars, OUT &f) != OK)
      return;

   if (f <= (double)-LONG_MAX + DBL_EPSILON)
      returnVar->number = -LONG_MAX;
   ei (f >= (double)LONG_MAX - DBL_EPSILON)
      returnVar->number = LONG_MAX;
   else
      returnVar->number = (Long)f;
}

// "floor({float})" function
void
f_floor(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = floor(f);
   else
      returnVar->floatt = 0.0;
}

void
f_fmod(Arr(Var) argvars, Var* returnVar) {
   double   fx = 0.0, fy = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &fx) == OK && get_float_arg(argvars + 1, OUT &fy) == OK)
      returnVar->floatt = fmod(fx, fy);
   else
      returnVar->floatt = 0.0;
}

// "isinf()" function
void
f_isinf(Arr(Var) argvars, Var* returnVar) {
   if (argvars[0].tag == VAR_FLOAT && isinf(argvars[0].floatt))
      returnVar->number = argvars[0].floatt > 0.0 ? 1 : -1;
}

// "isnan()" function
void
f_isnan(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = argvars[0].tag == VAR_FLOAT && isnan(argvars[0].floatt);
}

//"log()" the logarithm function
void
f_log(Arr(Var) argvars, Var* returnVar) {
    double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = log(f);
   else
      returnVar->floatt = 0.0;
}

void
f_log10(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = log10(f);
   else
      returnVar->floatt = 0.0;
}

void
f_pow(Arr(Var) argvars, Var* returnVar) {
    double   fx = 0.0, fy = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &fx) == OK && get_float_arg(argvars + 1, OUT &fy) == OK)
      returnVar->floatt = pow(fx, fy);
   else
      returnVar->floatt = 0.0;
}

void
f_sqrt(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = sqrt(f);
   else
      returnVar->floatt = 0.0;
}

void
f_str2float(Arr(Var) argvars, Var* returnVar) {
   Boole skip_quotes = argvars[1].tag != VAR_UNKNOWN && tv_get_bool(&argvars[1]);

   CS p = skipwhite(tv_get_string_strict(&argvars[0]));
   Boole isneg = (*p == '-');

   if (*p == '+' || *p == '-')
      p = skipwhite(p + 1);
   (void)string2float(p, OUT &returnVar->floatt, skip_quotes);
   if (isneg)
      returnVar->floatt *= -1;
   returnVar->tag = VAR_FLOAT;
}

void
f_tan(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = tan(f);
   else
      returnVar->floatt = 0.0;
}

// "tanh()" function
void
f_tanh(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = tanh(f);
   else
      returnVar->floatt = 0.0;
}

// "trunc({float})" function
void
f_trunc(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      // trunc() is not in C90, use floor() or ceil() instead.
      returnVar->floatt = f > 0 ? floor(f) : ceil(f);
   else
      returnVar->floatt = 0.0;
}

// "round({float})" function
void
f_round(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = round(f);
   else
      returnVar->floatt = 0.0;
}

void
f_sin(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = sin(f);
   else
      returnVar->floatt = 0.0;
}

void
f_sinh(Arr(Var) argvars, Var* returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, OUT &f) == OK)
      returnVar->floatt = sinh(f);
   else
      returnVar->floatt = 0.0;
}


//}}}
//{{{testing ancillary functions

// Prepare "gap" for an assert error and add the sourcing position.
private void
prepare_assert_error(ArrayList *gap) {
   char    buf[NUMBUFLEN];
   CS sname = estack_sfile(ESTACK_NONE);

   ga_init2(gap, 1, 100);
   if (sname != NULL) {
      ga_concat(gap, sname);
      if (SOURCING_LNUM > 0)
          ga_concat(gap, (CS)" ");
   }
   if (SOURCING_LNUM > 0) {
      sprintf(buf, "line %ld", (long)SOURCING_LNUM);
      ga_concat(gap, (CS)buf);
   }
   if (sname != NULL || SOURCING_LNUM > 0)
      ga_concat(gap, (CS)": ");
   eeglFree(sname);
}

//Append "p[clen]" to "gap", escaping unprintable characters.
//Change NL to \n, CR to \r, etc.
private void
ga_concat_esc(ArrayList *gap, CS p, int clen) {
   Byte  buf[NUMBUFLEN];

   if (clen > 1) {
      MEMMOVE(buf, p, clen);
      buf[clen] = ZERO;
      ga_concat(gap, buf);
      return;
   }

   switch (*p) {
   case BS: ga_concat(gap, (CS)"\\b"); break;
   case ESC: ga_concat(gap, (CS)"\\e"); break;
   case FF: ga_concat(gap, (CS)"\\f"); break;
   case NL: ga_concat(gap, (CS)"\\n"); break;
   case TAB: ga_concat(gap, (CS)"\\t"); break;
   case ENTER: ga_concat(gap, (CS)"\\r"); break;
   case '\\': ga_concat(gap, (CS)"\\\\"); break;
   default:
      if (*p < ' ' || *p == 0x7f) {
         eeSnprintf(buf, NUMBUFLEN, "\\x%02x", *p);
         ga_concat(gap, buf);
      } else
         ga_append(gap, *p);
      break;
   }
}

//Append "str" to "gap", escaping unprintable characters.
//Changes NL to \n, CR to \r, etc.
private void
ga_concat_shorten_esc(ArrayList *gap, CS str) {
   CS p;
   CS s;
   Unt c;
   int clen;
   Byte  buf[NUMBUFLEN];
   int same_len;

   if (!str) {
      ga_concat(gap, (CS)"NULL");
      return;
   }

   for (p = str; *p != ZERO; ) {
      same_len = 1;
      s = p;
      c = mb_cptr2char_adv(&s);
      clen = s - p;
      while (*s != ZERO && c == mb_ptr2char(s)) {
         ++same_len;
         s += clen;
      }
      if (same_len > 20) {
         ga_concat(gap, (CS)"\\[");
         ga_concat_esc(gap, p, clen);
         ga_concat(gap, (CS)" occurs ");
         eeSnprintf(buf, NUMBUFLEN, "%d", same_len);
         ga_concat(gap, buf);
         ga_concat(gap, (CS)" times]");
         p = s;
      } else {
         ga_concat_esc(gap, p, clen);
         p += clen;
      }
   }
}

//Fill "gap" with information about an assert error.
private void
fill_assert_error(
   ArrayList   *gap,
   Var   *opt_msg_tv,
   CS exp_str,
   Var* exp_tv_arg,
   Var* got_tv_arg,
   AssertKind assKind
) {
   Byte   numbuf[NUMBUFLEN];
   Byte* tofree;
   Var   *exp_tv = exp_tv_arg;
   Var   *got_tv = got_tv_arg;
   int      did_copy = false;
   int      omitted = 0;

   if (opt_msg_tv->tag != VAR_UNKNOWN
       && !(opt_msg_tv->tag == VAR_STRING
         && (opt_msg_tv->string == NULL || *opt_msg_tv->string == ZERO))
   ) {
      ga_concat(gap, echo_string(opt_msg_tv, &tofree, numbuf, 0));
      eeglFree(tofree);
      ga_concat(gap, (CS)": ");
   }

   if (assKind == ASSERT_MATCH || assKind == ASSERT_NOTMATCH)
      ga_concat(gap, (CS)"Pattern ");
   ei (assKind == ASSERT_NOTEQUAL)
      ga_concat(gap, (CS)"Expected not equal to ");
   else
      ga_concat(gap, (CS)"Expected ");
   if (exp_str == NULL) {
      // When comparing dictionaries, drop the items that are equal, so that
      // it's a lot easier to see what differs.
      if (assKind != ASSERT_NOTEQUAL
         && exp_tv->tag == VAR_BAG && got_tv->tag == VAR_BAG
         && exp_tv->bag != NULL && got_tv->bag != NULL)
      {
         Bag   *exp_d = exp_tv->bag;
         Bag   *got_d = got_tv->bag;
         EeSetItem   *hi;
         DictItem   *item2;
         int      todo;

         did_copy = true;
         exp_tv->bag = allocBag();
         got_tv->bag = allocBag();

         todo = (int)exp_d->hashTable.count;
         FOR_ALL_HASHTAB_ITEMS(&exp_d->hashTable, hi, todo) {
            if (!HASHITEM_EMPTY(hi)) {
               item2 = bagFind(got_d, textOfItem(hi));
               if (!item2 || !tv_equal(&HI2DI(hi)->c, &item2->c, false)) {
                  // item of exp_d not present in got_d or values differ.
                  bagAddVar(exp_tv->bag, hi->hi_key, &HI2DI(hi)->c);
                  if (item2)
                     bagAddVar(got_tv->bag, hi->hi_key, &item2->c);
               } else
                  ++omitted;
               --todo;
            }
         }

         // Add items only present in got_d.
         todo = (int)got_d->hashTable.count;
         FOR_ALL_HASHTAB_ITEMS(&got_d->hashTable, hi, todo) {
            if (!HASHITEM_EMPTY(hi)) {
               item2 = bagFind(exp_d, textOfItem(hi));
               if (item2 == NULL)
                  // item of got_d not present in exp_d
                  bagAddVar(got_tv->bag, hi->hi_key, &HI2DI(hi)->c);
               --todo;
            }
         }
      }

      ga_concat_shorten_esc(gap, tv2string(exp_tv, &tofree, numbuf, 0));
      eeglFree(tofree);
   } else {
      if (assKind == ASSERT_FAILS)
          ga_concat(gap, (CS)"'");
      ga_concat_shorten_esc(gap, exp_str);
      if (assKind == ASSERT_FAILS)
          ga_concat(gap, (CS)"'");
   }
   if (assKind != ASSERT_NOTEQUAL) {
      if (assKind == ASSERT_MATCH)
         ga_concat(gap, (CS)" does not match ");
      ei (assKind == ASSERT_NOTMATCH)
         ga_concat(gap, (CS)" does match ");
      else
         ga_concat(gap, (CS)" but got ");
      ga_concat_shorten_esc(gap, tv2string(got_tv, &tofree, numbuf, 0));
      eeglFree(tofree);

      if (omitted != 0) {
         Byte buf[100];
         eeSnprintf(buf, 100, " - %d equal item%s omitted", omitted, omitted == 1 ? "" : "s");
         ga_concat(gap, (CS)buf);
      }
   }

   if (did_copy) {
      clearVar(exp_tv);
      clearVar(got_tv);
   }
}

private int
assert_equal_common(Arr(Var) argvars, AssertKind assKind) {
   if (tv_equal(&argvars[0], &argvars[1], false) != (assKind == ASSERT_EQUAL)) {
      ArrayList   ga;
      prepare_assert_error(&ga);
      fill_assert_error(&ga, &argvars[2], NULL, &argvars[0], &argvars[1], assKind);
      assert_error(&ga);
      ga_clear(&ga);
      return 1;
   }
   return 0;
}

private int
assert_match_common(Arr(Var) argvars, AssertKind assKind) {
   ArrayList   ga;
   Byte buf1[NUMBUFLEN];
   Byte buf2[NUMBUFLEN];
   CS pat = convertVarToString(&argvars[0], buf1);
   CS text = convertVarToString(&argvars[1], buf2);
   if (pat && text && pattern_match(pat, text, false) != (assKind == ASSERT_MATCH)) {
      prepare_assert_error(&ga);
      fill_assert_error(&ga, &argvars[2], NULL, &argvars[0], &argvars[1], assKind);
      assert_error(&ga);
      ga_clear(&ga);
      return 1;
    }
    return 0;
}

//Common for assert_true() and assert_false(). Return non-zero for failure.
private int
assert_bool(Arr(Var) argvars, int isTrue) {
   Boole error = false;
   ArrayList   ga;

   if (argvars[0].tag == VAR_BOOL && argvars[0].number == (isTrue ? VVAL_TRUE : VVAL_FALSE))
      return 0;
   if (argvars[0].tag != VAR_NUMBER
       || (varGetNumberChk(argvars, OUT &error) == 0) == isTrue
       || error
   ) {
      prepare_assert_error(&ga);
      fill_assert_error(&ga, &argvars[1],
         (CS)(isTrue ? "True" : "False"),
         NULL, &argvars[0], ASSERT_OTHER);
      assert_error(&ga);
      ga_clear(&ga);
      return 1;
   }
   return 0;
}

private void
assert_append_cmd_or_arg(ArrayList *gap, Arr(Var) argvars, CS cmd) {
   Byte numbuf[NUMBUFLEN];

   if (argvars[1].tag != VAR_UNKNOWN && argvars[2].tag != VAR_UNKNOWN) {
      Byte* tofree;
      ga_concat(gap, echo_string(&argvars[2], &tofree, numbuf, 0));
      eeglFree(tofree);
   } else
      ga_concat(gap, cmd);
}

//"assert_equal(expected, actual[, msg])" function
void
f_assert_equal(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_equal_common(argvars, ASSERT_EQUAL);
}

private int
assert_equalfile(Arr(Var) argvars) {
   Byte buf1[NUMBUFLEN];
   Byte buf2[NUMBUFLEN];
   CS fname1 = convertVarToString(&argvars[0], buf1);
   CS fname2 = convertVarToString(&argvars[1], buf2);

   if (!fname1 || !fname2)
      return 0;

   char   line1[200];
   char   line2[200];
   int      lineidx = 0;
   IObuff[0] = ZERO;
   FILE* fd1 = fopen((char *)fname1, READBIN);
   if (fd1 == NULL) {
      eeSnprintf(IObuff, IOSIZE, (char *)e_cant_read_file_str, fname1);
   } else {
      FILE* fd2 = fopen((char *)fname2, READBIN);
      if (fd2 == NULL) {
         fclose(fd1);
         eeSnprintf(IObuff, IOSIZE, (char *)e_cant_read_file_str, fname2);
      } else {
         int       c1, c2;
         long    count = 0;
         long    linecount = 1;

         for (;;) {
            c1 = fgetc(fd1);
            c2 = fgetc(fd2);
            if (c1 == EOF) {
               if (c2 != EOF)
                  STRCPY(IObuff, "first file is shorter");
               break;
            } ei (c2 == EOF) {
               STRCPY(IObuff, "second file is shorter");
               break;
            } else {
               line1[lineidx] = c1;
               line2[lineidx] = c2;
               ++lineidx;
               if (c1 != c2) {
                  eeSnprintf(IObuff, IOSIZE, "difference at byte %ld, line %ld", count, linecount);
                  break;
               }
            }
            ++count;
            if (c1 == NL) {
               ++linecount;
               lineidx = 0;
            } ei (lineidx + 2 == (int)sizeof(line1)) {
               MEMMOVE(line1, line1 + 100, lineidx - 100);
               MEMMOVE(line2, line2 + 100, lineidx - 100);
               lineidx -= 100;
            }
         }
         fclose(fd1);
         fclose(fd2);
      }
    }

   if (IObuff[0] != ZERO) {
      ArrayList   ga;
      prepare_assert_error(&ga);
      if (argvars[2].tag != VAR_UNKNOWN) {
          Byte numbuf[NUMBUFLEN];
          Byte* tofree;

          ga_concat(&ga, echo_string(&argvars[2], &tofree, numbuf, 0));
          eeglFree(tofree);
          ga_concat(&ga, (CS)": ");
      }
      ga_concat(&ga, IObuff);
      if (lineidx > 0) {
         line1[lineidx] = ZERO;
         line2[lineidx] = ZERO;
         ga_concat(&ga, (CS)" after \"");
         ga_concat(&ga, (CS)line1);
         if (STRCMP(line1, line2) != 0) {
            ga_concat(&ga, (CS)"\" vs \"");
            ga_concat(&ga, (CS)line2);
         }
         ga_concat(&ga, (CS)"\"");
      }
      assert_error(&ga);
      ga_clear(&ga);
      return 1;
   }

   return 0;
}

//"assert_equalfile(fname-one, fname-two[, msg])" function
void
f_assert_equalfile(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_equalfile(argvars);
}

//"assert_notequal(expected, actual[, msg])" function
void
f_assert_notequal(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_equal_common(argvars, ASSERT_NOTEQUAL);
}

//"assert_exception(string[, msg])" function
void
f_assert_exception(Arr(Var) argvars, Var* returnVar) {
   ArrayList   ga;

   CS error = convertVarToStringSingleUse(&argvars[0]);
   if (*get_EeglVar_str(VV_EXCEPTION) == ZERO) {
      prepare_assert_error(&ga);
      ga_concat(&ga, (CS)"v:exception is not set");
      assert_error(&ga);
      ga_clear(&ga);
      returnVar->number = 1;
    } ei (error && strstr((char *)get_EeglVar_str(VV_EXCEPTION), (char *)error) == NULL) {
      prepare_assert_error(&ga);
      fill_assert_error(&ga, &argvars[1], NULL, &argvars[0],
                 get_EeglVar_tv(VV_EXCEPTION), ASSERT_OTHER);
      assert_error(&ga);
      ga_clear(&ga);
      returnVar->number = 1;
   }
}

//"assert_fails(cmd [, error[, msg]])" function
void
f_assert_fails(Arr(Var) argvars, Var* returnVar) {
   ArrayList   ga;
   int      save_trylevel = trylevel;
   int      called_emsg_before = called_emsg;
   CS wrong_arg_msg = NULL;
   Byte* tofree = NULL;

   if (check_for_string_or_number_arg(argvars, 0) == FAIL
       || check_for_opt_string_or_list_arg(argvars, 1) == FAIL
       || (argvars[1].tag != VAR_UNKNOWN
      && (argvars[2].tag != VAR_UNKNOWN
          && (check_for_opt_number_arg(argvars, 3) == FAIL
         || (argvars[3].tag != VAR_UNKNOWN
             && check_for_opt_string_arg(argvars, 4) == FAIL)))))
      return;

   // trylevel must be zero for a ":throw" command to be considered failed
   trylevel = 0;
   suppress_errthrow = true;
   in_assert_fails = true;
   ++no_wait_return;

   CS cmd = convertVarToStringSingleUse(&argvars[0]);
   executeCommLine(cmd);

   // reset here for any errors reported below
   trylevel = save_trylevel;
   suppress_errthrow = false;

   if (called_emsg == called_emsg_before) {
      prepare_assert_error(&ga);
      ga_concat(&ga, (CS)"command did not fail: ");
      assert_append_cmd_or_arg(&ga, argvars, cmd);
      assert_error(&ga);
      ga_clear(&ga);
      returnVar->number = 1;
   } ei (argvars[1].tag != VAR_UNKNOWN) {
      Byte buf[NUMBUFLEN];
      CS expected;
      CS expected_str = NULL;
      int   error_found = false;
      int   error_found_index = 1;
      CS actual = emsg_assert_fails_msg  ? emsg_assert_fails_msg : S"[unknown]";

      if (argvars[1].tag == VAR_STRING) {
         expected = convertVarToString(&argvars[1], buf);
         error_found = expected == NULL || strstr((char *)actual, (char *)expected) == NULL;
      } ei (argvars[1].tag == VAR_LIST) {
         List   *list = argvars[1].list;
         Var   *tv;

         if (!list || list->len < 1 || list->len > 2) {
            wrong_arg_msg = e_assert_fails_second_arg;
            goto theend;
         }
         CHECK_LIST_MATERIALIZE(list);
         tv = &list->first->c;
         expected = convertVarToString(tv, buf);
         if (expected == NULL)
            goto theend;
         if (!pattern_match(expected, actual, false)) {
            error_found = true;
            expected_str = expected;
         } ei (list->len == 2) {
            // make a copy, an error in pattern_match() may free it
            tofree = actual = copyStr(get_EeglVar_str(VV_ERRMSG));
            tv = &list->lv_u.mat.last->c;
            expected = convertVarToString(tv, buf);
            if (expected == NULL)
               goto theend;
            if (!pattern_match(expected, actual, false)) {
               error_found = true;
               expected_str = expected;
            }
         }
      } else {
         wrong_arg_msg = e_assert_fails_second_arg;
         goto theend;
      }

      if (!error_found && argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN) {
         if (argvars[3].tag != VAR_NUMBER) {
            wrong_arg_msg = e_assert_fails_fourth_argument;
            goto theend;
         } ei (argvars[3].number >= 0 && argvars[3].number != emsg_assert_fails_lnum) {
            error_found = true;
            error_found_index = 3;
         }
         if (!error_found && argvars[4].tag != VAR_UNKNOWN) {
            if (argvars[4].tag != VAR_STRING) {
               wrong_arg_msg = e_assert_fails_fifth_argument;
               goto theend;
            } ei (argvars[4].string 
                  && !pattern_match(argvars[4].string, emsg_assert_fails_context, false)
            ) {
               error_found = true;
               error_found_index = 4;
            }
         }
      }

      if (error_found) {
         Var actual_tv;

         prepare_assert_error(&ga);
         if (error_found_index == 3) {
            actual_tv.tag = VAR_NUMBER;
            actual_tv.number = emsg_assert_fails_lnum;
         } ei (error_found_index == 4) {
            actual_tv.tag = VAR_STRING;
            actual_tv.string = emsg_assert_fails_context;
         } else {
            actual_tv.tag = VAR_STRING;
            actual_tv.string = actual;
         }
         fill_assert_error(
            &ga, &argvars[2], expected_str, &argvars[error_found_index], &actual_tv, ASSERT_FAILS
         );
         ga_concat(&ga, (CS)": ");
         assert_append_cmd_or_arg(&ga, argvars, cmd);
         assert_error(&ga);
         ga_clear(&ga);
         returnVar->number = 1;
      }
   }

theend:
   trylevel = save_trylevel;
   suppress_errthrow = false;
   in_assert_fails = false;
   anyEmsgG = false;
   gotInterruptG = false;
   msgColG = 0;
   --no_wait_return;
   need_wait_return = false;
   emsg_on_display = false;
   msg_scrolled = 0;
   lines_left = visibleRowsG;
   EE_CLEAR(emsg_assert_fails_msg);
   eeglFree(tofree);
   set_EeglVar_string(VV_ERRMSG, NULL, 0);
   if (wrong_arg_msg)
      emsg(_(wrong_arg_msg));
}

//"assert_false(actual[, msg])" function
void
f_assert_false(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_bool(argvars, false);
}

private int
assert_inrange(Arr(Var) argvars) {
   ArrayList   ga;
   Boole error = false;
   Byte expected_str[200];

   if (argvars[0].tag == VAR_FLOAT || argvars[1].tag == VAR_FLOAT || argvars[2].tag == VAR_FLOAT) {
      double flower = tv_get_float(&argvars[0]);
      double fupper = tv_get_float(&argvars[1]);
      double factual = tv_get_float(&argvars[2]);

      if (factual < flower || factual > fupper) {
         prepare_assert_error(&ga);
         eeSnprintf(expected_str, 200, "range %g - %g,", flower, fupper);
         fill_assert_error(&ga, &argvars[3], expected_str, NULL, &argvars[2], ASSERT_OTHER);
         assert_error(&ga);
         ga_clear(&ga);
         return 1;
      }
   } else {
      Long lower = varGetNumberChk(&argvars[0], OUT &error);
      Long upper = varGetNumberChk(&argvars[1], OUT &error);
      Long actual = varGetNumberChk(&argvars[2], OUT &error);

      if (error)
         return 0;
      if (actual < lower || actual > upper) {
         prepare_assert_error(&ga);
         eeSnprintf(expected_str, 200, "range %ld - %ld,", (long)lower, (long)upper);
         fill_assert_error(&ga, &argvars[3], expected_str, NULL, &argvars[2], ASSERT_OTHER);
         assert_error(&ga);
         ga_clear(&ga);
         return 1;
      }
   }
   return 0;
}

//"assert_inrange(lower, upper[, msg])" function
void
f_assert_inrange(Arr(Var) argvars, Var* returnVar) {
   if (check_for_float_or_nr_arg(argvars, 0) == FAIL
          || check_for_float_or_nr_arg(argvars, 1) == FAIL
          || check_for_float_or_nr_arg(argvars, 2) == FAIL
          || check_for_opt_string_arg(argvars, 3) == FAIL
   )
      return;

   returnVar->number = assert_inrange(argvars);
}

//"assert_match(pattern, actual[, msg])" function
void
f_assert_match(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_match_common(argvars, ASSERT_MATCH);
}

//"assert_notmatch(pattern, actual[, msg])" function
void
f_assert_notmatch(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_match_common(argvars, ASSERT_NOTMATCH);
}

//"assert_report(msg)" function
void
f_assert_report(Arr(Var) argvars, Var* returnVar) {
   ArrayList   ga;
   prepare_assert_error(OUT &ga);
   ga_concat(&ga, tv_get_string(&argvars[0]));
   assert_error(&ga);
   ga_clear(&ga);
   returnVar->number = 1;
}

//"assert_true(actual[, msg])" function
void
f_assert_true(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = assert_bool(argvars, true);
}

//"test_alloc_fail(id, countdown, repeat)" function
void
f_test_alloc_fail(Arr(Var) argvars, Var* returnVar UNUSED) {
    if (argvars[0].tag != VAR_NUMBER
       || argvars[0].number <= 0
       || argvars[1].tag != VAR_NUMBER
       || argvars[1].number < 0
       || argvars[2].tag != VAR_NUMBER
   )
      emsg(_(e_invalid_argument));
   else {
      alloc_fail_id = argvars[0].number;
      if (alloc_fail_id >= aid_last)
         emsg(_(e_invalid_argument));
      alloc_fail_countdown = argvars[1].number;
      alloc_fail_repeat = argvars[2].number;
      did_outofmem_msg = false;
   }
}

void
f_test_autochdir(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
}

void
f_test_feedinput(Arr(Var) argvars, Var* returnVar UNUSED) {
#ifdef USE_INPUT_BUF
   CS val = convertVarToStringSingleUse(&argvars[0]);
   if (val) {
      trash_input_buf();
      add_to_input_buf_csi(val, (int)STRLEN(val));
   }
#endif
}

//"test_getvalue({name})" function
void
f_test_getvalue(Arr(Var) argvars, Var* returnVar) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   CS  name = tv_get_string(&argvars[0]);

   if (STRCMP(name, (CS)"needFileinfoG") == 0)
      returnVar->number = needFileinfoG;
   else
      showErrFmtMsg(_(e_invalid_argument_str), name);
}

//"test_option_not_set({name})" function
void
f_test_option_not_set(Arr(Var) argvars, Var* returnVar UNUSED) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   CS name = tv_get_string(&argvars[0]);
   if (reset_optWasSet(name) == FAIL)
      showErrFmtMsg(_(e_invalid_argument_str), name);
}

//"test_override({name}, {val})" function
void
f_test_override(Arr(Var) argvars, Var* returnVar UNUSED) {
   CS name = S"";
   static int save_starting = -1;

   if (check_for_string_arg(argvars, 0) == FAIL || check_for_number_arg(argvars, 1) == FAIL)
      return;

   name = tv_get_string(&argvars[0]);
   int val = (int)tv_get_number(&argvars[1]);

   if (STRCMP(name, (CS)"redraw") == 0)
      disable_redraw_for_testing = val;
   ei (STRCMP(name, (CS)"redraw_flag") == 0)
      ignore_redraw_flag_for_testing = val;
   ei (STRCMP(name, (CS)"char_avail") == 0)
      disable_char_avail_for_testing = val;
   ei (STRCMP(name, (CS)"starting") == 0) {
      if (val) {
          if (save_starting < 0)
         save_starting = starting;
          starting = 0;
      } else {
          starting = save_starting;
          save_starting = -1;
      }
   } ei (STRCMP(name, (CS)"nfa_fail") == 0)
      nfa_fail_for_testing = val;
   ei (STRCMP(name, (CS)"no_query_mouse") == 0)
      no_query_mouse_for_testing = val;
   ei (STRCMP(name, (CS)"no_wait_return") == 0)
   no_wait_return = val;
    ei (STRCMP(name, (CS)"ui_delay") == 0)
   ui_delay_for_testing = val;
    ei (STRCMP(name, (CS)"unreachable") == 0)
   ignore_unreachable_code_for_testing = val;
    ei (STRCMP(name, (CS)"term_props") == 0)
   reset_term_props_on_termresponse = val;
    ei (STRCMP(name, (CS)"vterm_title") == 0)
   disable_vterm_title_for_testing = val;
   ei (STRCMP(name, S"uptime") == 0)
      overrideSysinfoUptimeG = val;
   ei (STRCMP(name, S"autoload") == 0)
      override_autoload = val;
   ei (STRCMP(name, S"defcompile") == 0)
      override_defcompile = val;
   ei (STRCMP(name, S"ALL") == 0) {
      disable_char_avail_for_testing = false;
      disable_redraw_for_testing = false;
      ignore_redraw_flag_for_testing = false;
      nfa_fail_for_testing = false;
      no_query_mouse_for_testing = false;
      ui_delay_for_testing = 0;
      reset_term_props_on_termresponse = false;
      overrideSysinfoUptimeG = -1;
      // ml_get_alloc_lines is not reset by "ALL"
      if (save_starting >= 0) {
          starting = save_starting;
          save_starting = -1;
      }
   } else
      showErrFmtMsg(_(e_invalid_argument_str), name);
}

//"test_refcount({expr})" function
void
f_test_refcount(Arr(Var) argvars, Var* returnVar) {
   int retval = -1;

   switch (argvars[0].tag) {
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_NUMBER:
   case VAR_BOOL:
   case VAR_FLOAT:
   case VAR_SPECIAL:
   case VAR_STRING:
      break;

   case VAR_JOB:
      if (argvars[0].job != NULL)
         retval = argvars[0].job->jv_refcount - 1;
      break;
   case VAR_CHANNEL:
      if (argvars[0].channel != NULL)
         retval = argvars[0].channel->refcount - 1;
      break;
   case VAR_FUNC:
      if (argvars[0].string != NULL) {
         UserFunc *fp;

         fp = find_func(argvars[0].string, false);
         if (fp)
            retval = fp->refcount;
      }
      break;
   case VAR_PARTIAL:
      if (argvars[0].partial != NULL)
         retval = argvars[0].partial->refcount - 1;
      break;
   case VAR_BLOB:
      if (argvars[0].blob != NULL)
         retval = argvars[0].blob->refcount - 1;
      break;
   case VAR_LIST:
      if (argvars[0].list != NULL)
         retval = argvars[0].list->refcount - 1;
      break;
   case VAR_BAG:
      if (argvars[0].bag != NULL)
         retval = argvars[0].bag->refcount - 1;
      break;
   }

   returnVar->tag = VAR_NUMBER;
   returnVar->number = retval;
}

void
f_test_garbagecollect_now(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
    // This is dangerous, any Lists and Dicts used internally may be freed while still in use.
    if (!get_EeglVar_nr(VV_TESTING))
   emsg(_(e_calling_test_garbagecollect_now_while_v_testing_is_not_set));
    else
   garbage_collect(true);
}

void
f_test_garbagecollect_soon(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
   may_garbage_collect = true;
}

void
f_test_ignore_error(Arr(Var) argvars, Var* returnVar UNUSED) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   ignore_error_for_testing(tv_get_string(&argvars[0]));
}

void
f_test_null_blob(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_BLOB;
   returnVar->blob = NULL;
}

void
f_test_null_channel(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_CHANNEL;
   returnVar->channel = NULL;
}

void
f_test_null_dict(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar_dict_set(returnVar, NULL);
}

void
f_test_null_job(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_JOB;
   returnVar->job = NULL;
}

void
f_test_null_list(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar_list_set(returnVar, NULL);
}

void
f_test_null_function(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_FUNC;
   returnVar->string = NULL;
}

void
f_test_null_partial(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_PARTIAL;
   returnVar->partial = NULL;
}

void
f_test_null_string(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
}

void
f_test_unknown(Arr(Var) argvars UNUSED, Var* returnVar) {
    returnVar->tag = VAR_UNKNOWN;
}

void
f_test_void(Arr(Var) argvars UNUSED, Var* returnVar) {
    returnVar->tag = VAR_VOID;
}

void
f_test_setmouse(Arr(Var) argvars, Var* returnVar UNUSED) {
   if (argvars[0].tag != VAR_NUMBER || argvars[1].tag != VAR_NUMBER) {
      emsg(_(e_invalid_argument));
      return;
   }

   mouseRowG = (time_t)tv_get_number(&argvars[0]) - 1;
   mouseColG = (time_t)tv_get_number(&argvars[1]) - 1;
}

void
f_test_settime(Arr(Var) argvars, Var* returnVar UNUSED) {
   time_for_testing = (time_t)tv_get_number(&argvars[0]);
}

//}}}
//{{{indexing and slices

// Check if "var" can have an [index] or [sli:ce]
int
check_can_index(Var* var, int evaluate, int verbose) {
   switch (var->tag) {
   case VAR_FUNC:
   case VAR_PARTIAL:
      if (verbose)
         emsg(_(e_cannot_index_a_funcref));
      return FAIL;
   case VAR_FLOAT:
      if (verbose)
         emsg(_(e_using_float_as_string));
      return FAIL;
   case VAR_BOOL:
   case VAR_SPECIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
      if (evaluate) {
         emsg(_(e_cannot_index_special_variable));
         return FAIL;
      }
      // FALLTHROUGH

   case VAR_STRING:
   case VAR_LIST:
   case VAR_BAG:
   case VAR_BLOB:
      break;
   case VAR_NUMBER:
      break;
   }
   return OK;
}

// Apply index or range to "returnVar".
// "var1" is the first index, NULL for [:expr].
// "var2" is the second index, NULL for [expr] and [expr: ]
// "exclusive" is true for slice(): second index is exclusive, use character index for string.
// Alternatively, "key" is not NULL, then key[keylen] is the dict index.
int
eval_index_inner(
   Var* returnVar,
   int is_range,
   Var* var1,
   Var* var2,
   int exclusive,
   CS key,
   int keylen,
   int verbose
) {
   Long n1, n2 = 0;
   long len;

   n1 = 0;
   if (var1 && returnVar->tag != VAR_BAG)
      n1 = tv_get_number(var1);

   if (is_range) {
      if (returnVar->tag == VAR_BAG) {
         if (verbose)
            emsg(_(e_cannot_slice_dictionary));
         return FAIL;
      }
      if (var2)
         n2 = tv_get_number(var2);
      else
         n2 = LONG_MAX;
   }

   switch (returnVar->tag) {
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
   case VAR_FUNC:
   case VAR_PARTIAL:
   case VAR_FLOAT:
   case VAR_BOOL:
   case VAR_SPECIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
   case VAR_NUMBER:
   case VAR_STRING: {
      CS s = tv_get_string(returnVar);

      len = (long)STRLEN(s);
      if (exclusive) {
         if (is_range)
            s = string_slice(s, n1, n2, exclusive);
         else
            s = char_from_string(s, n1);
      } ei (is_range) {
         // The resulting variable is a substring.  If the indexes
         // are out of range the result is empty.
         if (n1 < 0) {
            n1 = len + n1;
            if (n1 < 0)
                n1 = 0;
         }
         if (n2 < 0)
            n2 = len + n2;
         ei (n2 >= len)
            n2 = len;
         if (n1 >= len || n2 < 0 || n1 > n2)
            s = NULL;
         else
            s = copySubstr(s + n1, n2 - n1 + 1);
      } else {
         // The resulting variable is a string of a single
         // character.  If the index is too big or negative the result is empty.
         if (n1 >= len || n1 < 0)
            s = NULL;
         else
            s = copySubstr(s + n1, 1);
      }
      clearVar(returnVar);
      returnVar->tag = VAR_STRING;
      returnVar->string = s;
      }
      break;

   case VAR_BLOB:
      blob_slice_or_index(returnVar->blob, is_range, n1, n2, exclusive, returnVar);
      break;

   case VAR_LIST:
      if (var1 == NULL)
         n1 = 0;
      if (var2 == NULL)
         n2 = LONG_MAX;
      if (list_slice_or_index(returnVar->list, is_range, n1, n2, exclusive, returnVar, verbose) 
            == FAIL)
         return FAIL;
      break;

   case VAR_BAG: {
      if (!key) {
         key = convertVarToStringSingleUse(var1);
         if (!key)
            return FAIL;
      }

      DictItem* item = bagFind(returnVar->bag, mbText(key));

      if (item == NULL) {
         if (verbose) {
            if (keylen > 0)
               key[keylen] = ZERO;
            showErrFmtMsg(_(e_key_not_present_in_dictionary_str), key);
         }
         return FAIL;
      }

      Var tmp;
      copy_tv(OUT &tmp, &item->c);
      clearVar(returnVar);
      *returnVar = tmp;
      }
      break;
   }
   return OK;
}


//}}}
//}}}
//{{{blobs

// Blob support by Yasuhiro Matsumoto

// Allocate an empty blob. Caller should take care of the reference count.
Blob *
blob_alloc(void) {
   Blob *blob = ALLOC_CLEAR_ONE_ID(Blob, aid_blob_alloc);
   if (blob)
      ga_init2(&blob->c, 1, 100);
   return blob;
}

// Allocate an empty blob for a return value, with reference count set. Returns OK or FAIL.
int
returnVar_blob_alloc(Var* returnVar) {
   Blob   *b = blob_alloc();

   if (!b)
      return FAIL;

   returnVar_blob_set(returnVar, b);
   return OK;
}

// Set a blob as the return value.
void
returnVar_blob_set(Var* returnVar, Blob *b) {
   returnVar->tag = VAR_BLOB;
   returnVar->blob = b;
   if (b != NULL)
      ++b->refcount;
}

int
blob_copy(Blob *from, Var *to) {
   to->tag = VAR_BLOB;
   to->lock = 0;
   if (!from) {
      to->blob = NULL;
      return OK;
   }

   if (returnVar_blob_alloc(to) == FAIL)
      return FAIL;

   int len = from->c.len;
   if (len > 0) {
      to->blob->c.c = eeMemsave(from->c.c, len);
      if (to->blob->c.c == NULL)
         len = 0;
   }
   to->blob->c.len = len;
   to->blob->c.cap = len;

   return OK;
}

void
blob_free(Blob *b) {
   ga_clear(&b->c);
   eeglFree(b);
}

// Unreference a blob: decrement the reference count and free it when it becomes zero.
void
blob_unref(Blob *b) {
   if (b && --b->refcount <= 0)
      blob_free(b);
}

// Get the length of data.
long
blob_len(Blob *b) {
   if (!b)
      return 0L;
   return b->c.len;
}

// Get byte "idx" in blob "b". Caller must check that "idx" is valid.
int
blob_get(Blob *b, int idx) {
   return ((Byte*)b->c.c)[idx];
}

// Store one byte "byte" in blob "blob" at "idx". Caller must make sure that "idx" is valid.
void
blob_set(Blob* blob, int idx, int byte) {
   ((Byte*)blob->c.c)[idx] = byte;
}

// Store one byte "byte" in blob "blob" at "idx". Append one byte if needed.
void
blob_set_append(Blob *blob, int idx, int byte) {
   ArrayList *gap = &blob->c;

   // Allow for appending a byte.  Setting a byte beyond
   // the end is an error otherwise.
   if (idx < gap->len || (idx == gap->len && ga_grow(gap, 1) == OK)) {
      blob_set(blob, idx, byte);
      if (idx == gap->len)
         ++gap->len;
   }
}

// Return true when two blobs have exactly the same values.
int
blob_equal(Blob   *b1, Blob   *b2) {
   int       i;
   int       len1 = blob_len(b1);
   int       len2 = blob_len(b2);

   // empty and NULL are considered the same
   if (len1 == 0 && len2 == 0)
      return true;
   if (b1 == b2)
      return true;
   if (len1 != len2)
      return false;
   for (i = 0; i < b1->c.len; i++)
      if (blob_get(b1, i) != blob_get(b2, i)) return false;
   return true;
}

// Convert a blob to a readable form: "0z00112233.44556677.8899"
CS
blob2string(Blob *blob, Byte **tofree, Byte *numbuf) {
   int      i;
   ArrayList    ga;

   if (blob == NULL) {
      *tofree = NULL;
      return (CS)"0z";
   }

   // Store bytes in the growarray.
   ga_init2(&ga, 1, 4000);
   ga_concat(&ga, (CS)"0z");
   for (i = 0; i < blob_len(blob); i++) {
      if (i > 0 && (i & 3) == 0)
         ga_concat(&ga, (CS)".");
      eeSnprintf(numbuf, NUMBUFLEN, "%02X", blob_get(blob, i));
      ga_concat(&ga, numbuf);
   }
   ga_append(&ga, ZERO);
   *tofree = ga.c;
   return *tofree;
}

// Convert a string variable, in the format of blob2string(), to a blob. Return NULL when 
// the conversion failed.
Blob*
string2blob(CS str) {
   Blob  *blob = blob_alloc();
   if (!blob)
      return NULL;
      
   CS s = str;
   if (s[0] != '0' || (s[1] != 'z' && s[1] != 'Z'))
      goto failed;
   s += 2;
   while (eeIsXDigit(*s)) {
      if (!eeIsXDigit(s[1]))
         goto failed;
      ga_append(&blob->c, (hex2nr(s[0]) << 4) + hex2nr(s[1]));
      s += 2;
      if (*s == '.' && eeIsXDigit(s[1]))
          ++s;
   }
   if (*skipwhite(s) != ZERO)
      goto failed;  // text after final digit

   ++blob->refcount;
   return blob;

failed:
   blob_free(blob);
   return NULL;
}

//Return a slice of 'blob' from index 'n1' to 'n2' in 'returnVar'.  The length of
//the blob is 'len'.  Returns an empty blob if the indexes are out of range.
private int
blob_slice(
   Blob      *blob,
   long      len,
   Long   n1,
   Long   n2,
   int      exclusive,
   Var   *returnVar)
{
   if (n1 < 0) {
      n1 = len + n1;
      if (n1 < 0)
          n1 = 0;
   }
   if (n2 < 0)
      n2 = len + n2;
   ei (n2 >= len)
      n2 = len - (exclusive ? 0 : 1);
   if (exclusive)
      --n2;
   if (n1 >= len || n2 < 0 || n1 > n2) {
      clearVar(returnVar);
      returnVar->tag = VAR_BLOB;
      returnVar->blob = NULL;
   } else {
      Blob  *new_blob = blob_alloc();
      long    i;

      if (new_blob != NULL) {
         if (ga_grow(&new_blob->c, n2 - n1 + 1) == FAIL) {
            blob_free(new_blob);
            return FAIL;
         }
         new_blob->c.len = n2 - n1 + 1;
         for (i = n1; i <= n2; i++)
            blob_set(new_blob, i - n1, blob_get(blob, i));

         clearVar(returnVar);
         returnVar_blob_set(returnVar, new_blob);
      }
   }

   return OK;
}

//Return the byte value in 'blob' at index 'idx' in 'returnVar'.  If the index is
//too big or negative that is an error.  The length of the blob is 'len'.
private int
blob_index(
   Blob      *blob,
   int      len,
   Long   idx,
   Var   *returnVar)
{
   // The resulting variable is a byte value.
   // If the index is too big or negative that is an error.
   if (idx < 0)
      idx = len + idx;
   if (idx < len && idx >= 0) {
      int v = blob_get(blob, idx);

      clearVar(returnVar);
      returnVar->tag = VAR_NUMBER;
      returnVar->number = v;
   } else {
      showErrFmtMsg(_(e_blob_index_out_of_range_nr), idx);
      return FAIL;
   }

   return OK;
}

int
blob_slice_or_index(
   Blob      *blob,
   int      is_range,
   Long   n1,
   Long   n2,
   int      exclusive,
   Var   *returnVar)
{
   long   len = blob_len(blob);

   if (is_range)
      return blob_slice(blob, len, n1, n2, exclusive, returnVar);
   else
      return blob_index(blob, len, n1, returnVar);
}

// Check if "n1"- is a valid index for a blobl with length "bloblen".
int
check_blob_index(long bloblen, Long n1, int quiet) {
   if (n1 < 0 || n1 > bloblen) {
      if (!quiet)
          showErrFmtMsg(_(e_blob_index_out_of_range_nr), n1);
      return FAIL;
   }
   return OK;
}

// Check if "n1"-"n2" is a valid range for a blob with length "bloblen".
int
check_blob_range(long bloblen, Long n1, Long n2, int quiet) {
   if (n2 < 0 || n2 >= bloblen || n2 < n1) {
      if (!quiet)
          showErrFmtMsg(_(e_blob_index_out_of_range_nr), n2);
      return FAIL;
   }
   return OK;
}

//Set bytes "n1" to "n2" (inclusive) in "dest" to the value of "src". Caller must make sure 
//"src" is a blob. Returns FAIL if the number of bytes does not match.
int
blob_set_range(Blob *dest, long n1, long n2, Var *src) {
   if (n2 - n1 + 1 != blob_len(src->blob)) {
      emsg(_(e_blob_value_does_not_have_right_number_of_bytes));
      return FAIL;
   }

   int ir = 0;
   
   for (int il = n1; il <= n2; il++)
      blob_set(dest, il, blob_get(src->blob, ir++));
   return OK;
}

// "add(blob, item)" function
void
blob_add(Arr(Var) argvars, Var* returnVar) {
   Blob   *b = argvars[0].blob;
   Boole error = false;

   if (!b) {
      return;
   }

   if (value_check_lock(b->lock, text(N_("add() argument")), true))
      return;

   Long n = varGetNumberChk(&argvars[1], &error);
   if (error)
      return;

   ga_append(&b->c, (int)n);
   copy_tv(OUT returnVar, &argvars[0]);
}

// "remove({blob}, {idx} [, {end}])" function
void
blob_remove(Arr(Var) argvars, Var* returnVar, CS arg_errmsg) {
   Blob* b = argvars[0].blob;
   Boole error = false;
   CS p;

   if (b && value_check_lock(b->lock, mbText(arg_errmsg), true))
      return;

   long idx = (long)varGetNumberChk(&argvars[1], &error);
   if (error)
      return;

   int len = blob_len(b);

   if (idx < 0)
      // count from the end
      idx = len + idx;
   if (idx < 0 || idx >= len) {
      showErrFmtMsg(_(e_blob_index_out_of_range_nr), idx);
      return;
   }
   if (argvars[2].tag == VAR_UNKNOWN) {
      // Remove one item, return its value.
      p = (CS)b->c.c;
      returnVar->number = (Long) *(p + idx);
      MEMMOVE(p + idx, p + idx + 1, (Unt)len - idx - 1);
      --b->c.len;
      return;
   }

   // Remove range of items, return blob with values.
   long end = (long)varGetNumberChk(&argvars[2], &error);
   if (error)
      return;
   if (end < 0)
   // count from the end
   end = len + end;
   if (end >= len || idx > end) {
      showErrFmtMsg(_(e_blob_index_out_of_range_nr), end);
      return;
   }
   Blob* newblob = blob_alloc();
   if (newblob == NULL)
      return;
   newblob->c.len = end - idx + 1;
   if (ga_grow(&newblob->c, end - idx + 1) == FAIL) {
      eeglFree(newblob);
      return;
   }
   p = (CS)b->c.c;
   MEMMOVE((CS)newblob->c.c, p + idx, (Unt)(end - idx + 1));
   ++newblob->refcount;
   returnVar->tag = VAR_BLOB;
   returnVar->blob = newblob;

   if (len - end - 1 > 0)
      MEMMOVE(p + idx, p + end + 1, (Unt)(len - end - 1));
   b->c.len -= end - idx + 1;
}

//Implementation of map() and filter() for a Blob.  Apply "expr" to every
//number in Blob "blob_arg" and return the result in "returnVar".
void
blob_filter_map(
   Blob* blob_arg,
   FilterMap filtermap,
   Var* expr,
   CS arg_errmsg,
   Var* returnVar
) {
   Blob   *b = blob_arg;
   int      i;
   Var   tv;
   Long   val;
   Blob   *b_ret;
   int idx = 0;
   int rem;
   Var newtv;

   if (filtermap == FILTERMAP_MAPNEW) {
      returnVar->tag = VAR_BLOB;
      returnVar->blob = NULL;
   }
   if (!b || (filtermap == FILTERMAP_FILTER && value_check_lock(b->lock, mbText(arg_errmsg), true)))
      return;

   b_ret = b;
   if (filtermap == FILTERMAP_MAPNEW) {
      if (blob_copy(b, returnVar) == FAIL)
         return;
      b_ret = returnVar->blob;
   }

   // set_EeglVar_nr() doesn't set the type
   set_EeglVar_type(VV_KEY, VAR_NUMBER);

   int prev_lock = b->lock;
   if (b->lock == 0)
      b->lock = VAR_LOCKED;

   for (i = 0; i < b->c.len; i++) {
      tv.tag = VAR_NUMBER;
      val = blob_get(b, i);
      tv.number = val;
      set_EeglVar_nr(VV_KEY, idx);
      if (filter_map_one(&tv, expr, filtermap, &newtv, &rem) == FAIL || anyEmsgG)
         break;
      if (filtermap != FILTERMAP_FOREACH) {
         if (newtv.tag != VAR_NUMBER && newtv.tag != VAR_BOOL) {
            clearVar(&newtv);
            emsg(_(e_invalid_operation_for_blob));
            break;
         }
         if (filtermap != FILTERMAP_FILTER) {
            if (newtv.number != val)
               blob_set(b_ret, i, newtv.number);
         } ei (rem) {
            CS p = (CS)blob_arg->c.c;
            MEMMOVE(p + i, p + i + 1, (Unt)b->c.len - i - 1);
            --b->c.len;
            --i;
         }
      }
      ++idx;
    }

   b->lock = prev_lock;
}

// "insert(blob, {item} [, {idx}])" function
void
blob_insert_func(Arr(Var) argvars, Var* returnVar) {
   Blob   *b = argvars[0].blob;
   long   before = 0;
   Boole error = false;
   int      val, len;
   CS p;

   if (!b) {
      return;
   }

   if (value_check_lock(b->lock, text(N_("insert() argument")), true))
      return;

   len = blob_len(b);
   if (argvars[2].tag != VAR_UNKNOWN) {
      before = (long)varGetNumberChk(&argvars[2], &error);
      if (error)
         return;      // type error; errmsg already given
      if (before < 0 || before > len) {
         showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[2]));
         return;
      }
   }
    val = varGetNumberChk(&argvars[1], &error);
   if (error)
   return;
   if (val < 0 || val > 255) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[1]));
      return;
   }

   if (ga_grow(&b->c, 1) == FAIL)
      return;
   p = (CS)b->c.c;
   MEMMOVE(p + before + 1, p + before, (Unt)len - before);
   *(p + before) = val;
   ++b->c.len;

   copy_tv(OUT returnVar, &argvars[0]);
}

// Implementation of reduce() for Blob "argvars[0]" using the function "expr"
// starting with the optional initial value "argvars[2]" and return the result in "returnVar".
void
blob_reduce(Var* argvars, Var* expr, Var* returnVar) {
   Blob* b = argvars[0].blob;
   int called_emsg_start = called_emsg;
   int r;
   Var initial;
   Var argv[3];
   int i;

   if (argvars[2].tag == VAR_UNKNOWN) {
      if (b == NULL || b->c.len == 0) {
          showErrFmtMsg(_(e_reduce_of_an_empty_str_with_no_initial_value), "Blob");
          return;
      }
      initial.tag = VAR_NUMBER;
      initial.number = blob_get(b, 0);
      i = 1;
   } ei (check_for_number_arg(argvars, 2) == FAIL)
      return;
   else {
      initial = argvars[2];
      i = 0;
   }

   copy_tv(OUT returnVar, &initial);
   if (!b)
      return;

   for ( ; i < b->c.len; i++) {
      argv[0] = *returnVar;
      argv[1].tag = VAR_NUMBER;
      argv[1].number = blob_get(b, i);

      r = eval_expr_typval(expr, true, argv, 2, returnVar);

      clearVar(&argv[0]);
      if (r == FAIL || called_emsg != called_emsg_start)
          return;
   }
}

void
blob_reverse(Blob* b, Var* returnVar) {
   int len = blob_len(b);

   for (int i = 0; i < len / 2; i++) {
      int tmp = blob_get(b, i);

      blob_set(b, i, blob_get(b, len - i - 1));
      blob_set(b, len - i - 1, tmp);
   }
   returnVar_blob_set(returnVar, b);
}

void
f_blob2list(Arr(Var) argvars, Var* returnVar) {
   allocReturnList(returnVar);

   if (check_for_blob_arg(argvars, 0) == FAIL)
      return;

   Blob* blob = argvars->blob;
   List* l = returnVar->list;
   for (int i = 0; i < blob_len(blob); i++)
      list_append_number(l, blob_get(blob, i));
}

void
f_list2blob(Arr(Var) argvars, Var* returnVar) {
   if (returnVar_blob_alloc(returnVar) == FAIL)
      return;
      
   Blob* blob = returnVar->blob;

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   List* l = argvars->list;
   if (l == NULL)
      return;

   CHECK_LIST_MATERIALIZE(l);
   
   ListItem* li;
   FOR_ALL_LIST_ITEMS(l, li) {
      Boole error = false;
      Long n = varGetNumberChk(&li->c, &error);
      if (error == true || n < 0 || n > 255) {
         if (!error)
            showErrFmtMsg(_(e_invalid_value_for_blob_nr), n);
         ga_clear(&blob->c);
         return;
      }
      ga_append(&blob->c, n);
   }
}

// Add the bytes from "str" to "blob".
private void
blob_from_string(CS str, Blob* blob) {
   Unt len = STRLEN(str);

   for (Unt i = 0; i < len; i++) {
      int ch = str[i];

      if (str[i] == NL)
         // Translate newlines in the string to ZERO character
         ch = ZERO;

      ga_append(&blob->c, ch);
   }
}

//Return a string created from the bytes in blob starting at "start_idx". A NL character in the 
//blob indicates end of string. A ZERO character in the blob is translated to a NL.
//On return, "start_idx" points to next byte to process in blob.
private CS
string_from_blob(Blob *blob, long *start_idx) {
   ArrayList str_ga;
   int idx;

   ga_init2(&str_ga, sizeof(char), 80);

   long blen = blob_len(blob);

   for (idx = *start_idx; idx < blen; idx++) {
      Byte byte = (Byte)blob_get(blob, idx);
      if (byte == NL) {
         idx++;
         break;
      }

      if (byte == ZERO)
         byte = NL;

      ga_append(&str_ga, byte);
   }

   ga_append(&str_ga, ZERO);

   CS ret_str = copyStr(str_ga.c);
   *start_idx = idx;

   ga_clear(&str_ga);
   return ret_str;
}

//"blob2str()" function Converts a blob to a string, ensuring valid UTF-8 encoding.
void
f_blob2str(Arr(Var) argvars, OUT Var* returnVar) {
   if (check_for_blob_arg(argvars, 0) == FAIL || check_for_oself_arg(argvars, 1) == FAIL)
      return;

   allocReturnList(returnVar);

   Blob* blob = argvars->blob;
   if (blob == NULL)
      return;
   int blen = blob_len(blob);

   long idx = 0;
   while (idx < blen) {
      CS str = string_from_blob(blob, &idx);
      if (!str)
         break;

      int ret = list_append_string(returnVar->list, str, -1);
      if (ret == FAIL)
         break;
   }
}

// "str2blob()" function
void
f_str2blob(Arr(Var) argvars, OUT Var* returnVar) {
   if (confirmVarIsList(argvars, 0) == FAIL || check_for_oself_arg(argvars, 1) == FAIL)
      return;

   if (returnVar_blob_alloc(returnVar) == FAIL)
      return;

   Blob* blob = returnVar->blob;

   List* list = argvars[0].list;
   if (!list)
      return;

   ListItem* li;
   FOR_ALL_LIST_ITEMS(list, li) {
      if (li->c.tag != VAR_STRING)
         continue;

      CS str = li->c.string;
      if (!str)
         str = E;

      if (li != list->first)
         // Each list string item is separated by a newline in the blob
         ga_append(&blob->c, NL);

      blob_from_string(str, blob);
   }
}



//}}}
//{{{fuzzy searchin'

//Fuzzy matching algorithm and related functions
//
//Portions of this file are adapted from fzy (https://github.com/jhawthorn/fzy)
//Original code:
//  Copyright (c) 2014 John Hawthorn
//  Licensed under the MIT License.
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

private int fuzzy_match_item_compare(const void *s1, const void *s2);
private void fuzzy_match_in_list(
      List *l, Byte *str, int matchseq, Byte *key, Callback *item_cb, int retmatchpos, 
      List *fmatchlist, long max_matches
);
private void do_fuzzymatch(Arr(Var) argvars, OUT Var* returnVar, int retmatchpos);
private int fuzzyMatchStr_compare(const void *s1, const void *s2);
private int fuzzy_match_func_compare(const void *s1, const void *s2);
private void sortFnNamesByScore(Arr(FuzzyMatch) fm, int sz);

private double match_positions(CS needle, CS haystack, Unt* positions);
private int has_match(CS needle, CS haystack);

#define SCORE_MAX INFINITY
#define SCORE_MIN (-INFINITY)
#define SCORE_SCALE 1000

typedef struct {
   int      idx;      // used for stable sort
   ListItem* item;
   int score;
   List* lmatchpos;
   CS pat;
   CS itemstr;
   int itemstr_allocated;
   int startpos;
} FuzzyItem;

void
addFuzzyMatch(FuzzyMatch m, OUT Fuzzy* t) {
   if (t->len == t->cap) {
      Arr(FuzzyMatch) newContent = allocateArray(t->cap*2, FuzzyMatch, t->a);
      if (t->len > 0)
         memcpy(newContent, t->c, t->len*sizeof(FuzzyMatch));
      t->c = newContent;
      t->cap *= 2;
   }
   m.idx = t->len;
   t->c[t->len] = m;
   t->len++;
}

//Return true if "pat_arg" matches "str". Also returns the match score in
//"outScore" and the matching character positions in "matches".
int
fuzzy_match(
   CS str,
   CS pat_arg,
   int matchseq,
   int* outScore,
   Arr(Unt) matches,
   int maxMatches
) {
   int complete = false;
   int score = 0;
   int numMatches = 0;
   double fzy_score;

   *outScore = 0;

   CS save_pat = copyStr(pat_arg);
   CS pat = save_pat;
   CS p = pat;

   // Try matching each word in 'pat_arg' in 'str'
   while (true) {
      if (matchseq)
         complete = true;
      else {
         // Extract one word from the pattern (separated by space)
         p = skipwhite(p);
         if (*p == ZERO)
            break;
         pat = p;
         while (*p != ZERO && !SPACE_OR_TAB(mb_ptr2char(p))) {
            MB_PTR_ADV(p);
         }
         if (*p == ZERO)      // processed all the words
            complete = true;
         *p = ZERO;
      }

      score = FUZZY_SCORE_NONE;
      if (has_match(pat, str)) {
         fzy_score = match_positions(pat, str, matches + numMatches);
         score = (fzy_score == SCORE_MIN) ? INT_MIN + 1
            : (fzy_score == SCORE_MAX) ? INT_MAX
            : (fzy_score < 0) ? (int)ceil(fzy_score * SCORE_SCALE - 0.5)
            : (int)floor(fzy_score * SCORE_SCALE + 0.5);
      }

      if (score == FUZZY_SCORE_NONE) {
         numMatches = 0;
         *outScore = FUZZY_SCORE_NONE;
         break;
      }

      if (score > 0 && *outScore > INT_MAX - score)
         *outScore = INT_MAX;
      ei (score < 0 && *outScore < INT_MIN + 1 - score)
         *outScore = INT_MIN + 1;
      else
         *outScore += score;

      numMatches += MB_CHARLEN(pat);

      if (complete || numMatches >= maxMatches)
          break;

      // try matching the next word
      ++p;
   }

   eeglFree(save_pat);
   return numMatches != 0;
}

//Sort the fuzzy matches in the descending order of the match score.
//For items with same score, retain the order using the index (stable sort)
private int
fuzzy_match_item_compare(const void *s1, const void *s2) {
   int v1 = ((FuzzyItem *)s1)->score;
   int v2 = ((FuzzyItem *)s2)->score;

   if (v1 == v2) {
      int exact_match1 = false, exact_match2 = false;
      CS pat = ((FuzzyItem *)s1)->pat;
      int patlen = (int)STRLEN(pat);
      int startpos = ((FuzzyItem *)s1)->startpos;
      exact_match1 = (startpos >= 0) && STRNCMP(pat,
         ((FuzzyItem *)s1)->itemstr + startpos, patlen) == 0;
      startpos = ((FuzzyItem *)s2)->startpos;
      exact_match2 = (startpos >= 0) && STRNCMP(pat,
         ((FuzzyItem *)s2)->itemstr + startpos, patlen) == 0;

      if (exact_match1 == exact_match2) {
         int idx1 = ((FuzzyItem *)s1)->idx;
         int idx2 = ((FuzzyItem *)s2)->idx;
         return idx1 == idx2 ? 0 : idx1 > idx2 ? 1 : -1;
      } ei (exact_match2)
         return 1;
      return -1;
   } else
      return v1 > v2 ? -1 : 1;
}

//Fuzzy search the string 'str' in a list of 'items' and return the matching
//strings in 'fmatchlist'.
//If 'matchseq' is true, then for multi-word search strings, match all the words in sequence.
//If 'items' is a list of strings, then search for 'str' in the list.
//If 'items' is a list of dicts, then either use 'key' to lookup the string
//for each item or use 'item_cb' Funcref function to get the string.
//If 'retmatchpos' is true, then return a list of positions where 'str' matches for each item.
private void
fuzzy_match_in_list(
   List* l,
   CS str,
   int matchseq,
   CS key,
   Callback* item_cb,
   int retmatchpos,
   List* fmatchlist,
   long max_matches
) {
   long match_count = 0;
   Unt matches[FUZZY_MATCH_MAX_LEN];

   long len = list_len(l);
   if (len == 0)
      return;
   if (max_matches > 0 && len > max_matches)
      len = max_matches;

   Arr(FuzzyItem) items = ALLOC_CLEAR_MULT(FuzzyItem, len);
   if (items == NULL)
      return;

   // For all the string items in items, get the fuzzy matching score
   ListItem* li;
   FOR_ALL_LIST_ITEMS(l, li) {
      int      score;
      Byte      *itemstr;
      Var   returnVar;
      int      itemstr_allocate = false;

      if (max_matches > 0 && match_count >= max_matches)
          break;

      itemstr = NULL;
      returnVar.tag = VAR_UNKNOWN;
      if (li->c.tag == VAR_STRING)   // list of strings
         itemstr = li->c.string;
      ei (li->c.tag == VAR_BAG && (key != NULL || item_cb->name != NULL)) {
         // For a dict, either use the specified key to lookup the string or
         // use the specified callback function to get the string.
         if (key)
            itemstr = bagGetString(li->c.bag, text(key), false);
         else {
            Var   argv[2];

            // Invoke the supplied callback (if any) to get the dict item
            li->c.bag->refcount++;
            argv[0].tag = VAR_BAG;
            argv[0].bag = li->c.bag;
            argv[1].tag = VAR_UNKNOWN;
            if (call_callback(item_cb, -1, &returnVar, 1, argv) != FAIL) {
               if (returnVar.tag == VAR_STRING) {
                  itemstr = returnVar.string;
                  itemstr_allocate = true;
               }
            }
            bagUnref(li->c.bag);
         }
      }

      if (itemstr != NULL
         && fuzzy_match(itemstr, str, matchseq, &score, matches, FUZZY_MATCH_MAX_LEN)
      ){
         items[match_count].idx = match_count;
         items[match_count].item = li;
         items[match_count].score = score;
         items[match_count].pat = str;
         items[match_count].startpos = matches[0];
         items[match_count].itemstr = itemstr_allocate ? copyStr(itemstr) : itemstr;
         items[match_count].itemstr_allocated = itemstr_allocate;

         // Copy the list of matching positions in itemstr to a list, if "retmatchpos" is set.
         if (retmatchpos) {
            items[match_count].lmatchpos = list_alloc();
            if (items[match_count].lmatchpos == NULL)
               goto done;

            int   j = 0;
            CS p = str;
            while (*p != ZERO && j < FUZZY_MATCH_MAX_LEN) {
               if (!SPACE_OR_TAB(mb_ptr2char(p)) || matchseq) {
                  if (list_append_number(items[match_count].lmatchpos, matches[j]) == FAIL)
                     goto done;
                  j++;
                }
                MB_PTR_ADV(p);
            }
         }
         ++match_count;
      }
      clearVar(&returnVar);
   }

   if (match_count > 0) {
      List      *retlist;

      //Sort the list by the descending order of the match score
      qsort((void *)items, (Unt)match_count, sizeof(FuzzyItem), fuzzy_match_item_compare);

      //For matchfuzzy(), return a list of matched strings.
      //      ['str1', 'str2', 'str3']
      //For matchfuzzypos(), return a list with three items.
      //The first item is a list of matched strings. The second item
      //is a list of lists where each list item is a list of matched
      //character positions. The third item is a list of matching scores.
      //  [['str1', 'str2', 'str3'], [[1, 3], [1, 3], [1, 3]]]
      if (retmatchpos) {
         li = list_find(fmatchlist, 0);
         if (li == NULL || li->c.list == NULL)
            goto done;
         retlist = li->c.list;
      } else
         retlist = fmatchlist;

      // Copy the matching strings to the return list
      for (int i = 0; i < match_count; i++) {
         if (list_append_tv(retlist, &items[i].item->c) == FAIL)
            goto done;
      }

      // next copy the list of matching positions
      if (retmatchpos) {
         li = list_find(fmatchlist, -2);
         if (li == NULL || li->c.list == NULL)
            goto done;
         retlist = li->c.list;

         for (int i = 0; i < match_count; i++) {
            if (items[i].lmatchpos != NULL) {
               if (list_append_list(retlist, items[i].lmatchpos) == OK)
                  items[i].lmatchpos = NULL;
               else
                  goto done;

            }
         }

         // copy the matching scores
         li = list_find(fmatchlist, -1);
         if (li == NULL || li->c.list == NULL)
            goto done;
         retlist = li->c.list;
         for (int i = 0; i < match_count; i++) {
            if (list_append_number(retlist, items[i].score) == FAIL)
               goto done;
         }
      }
   }

done:
   for (int i = 0; i < match_count; i++) {
      if (items[i].itemstr_allocated)
         eeglFree(items[i].itemstr);

      if (items[i].lmatchpos)
         list_free(items[i].lmatchpos);
   }
   eeglFree(items);
}

//Do fuzzy matching. Returns the list of matched strings in 'returnVar'.
//If 'retmatchpos' is true, also returns the matching character positions.
private void
do_fuzzymatch(Var* argvars, Var* returnVar, int retmatchpos) {
   Callback   cb;
   CS key = NULL;
   int      matchseq = false;
   long   max_matches = 0;

   CLEAR_POINTER(&cb);

   // validate and get the arguments
   if (argvars[0].tag != VAR_LIST || argvars[0].list == NULL) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list),
                 retmatchpos ? "matchfuzzypos()" : "matchfuzzy()");
      return;
   }
   if (argvars[1].tag != VAR_STRING || argvars[1].string == NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[1]));
      return;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      Bag      *d;
      DictItem   *di;

      if (check_for_nonnull_dict_arg(argvars, 2) == FAIL)
         return;

      // To search a dict, either a callback function or a key can be specified.
      d = argvars[2].bag;
      if ((di = bagFind(d, tConst("key"))) != NULL) {
         if (di->c.tag != VAR_STRING
             || di->c.string == NULL
             || *di->c.string == ZERO
         ) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "key", tv_get_string(&di->c));
            return;
         }
         key = tv_get_string(&di->c);
      } ei ((di = bagFind(d, tConst("text_cb"))) != NULL) {
         cb = get_callback(&di->c);
         if (cb.name == NULL) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str), "text_cb");
            return;
         }
      }

      if ((di = bagFind(d, tConst("limit"))) != NULL) {
         if (di->c.tag != VAR_NUMBER) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str), "limit");
            return;
         }
         max_matches = (long)varGetNumberChk(&di->c, NULL);
      }

      if (bagHasKey(d, tConst("matchseq")))
         matchseq = true;
   }

   // get the fuzzy matches
   allocReturnList(returnVar);
   if (retmatchpos) {

      //For matchfuzzypos(), a list with three items are returned. First
      //item is a list of matching strings, the second item is a list of
      //lists with matching positions within each string and the third item
      //is the list of scores of the matches.
      List* l = list_alloc();
      if (list_append_list(returnVar->list, l) == FAIL) {
         list_free(l);
         goto done;
      }
      l = list_alloc();
      if (list_append_list(returnVar->list, l) == FAIL) {
         list_free(l);
         goto done;
      }
      l = list_alloc();
      if (list_append_list(returnVar->list, l) == FAIL) {
         list_free(l);
         goto done;
      }
   }

   fuzzy_match_in_list(argvars[0].list, tv_get_string(&argvars[1]),
       matchseq, key, &cb, retmatchpos, returnVar->list, max_matches);

done:
   evFreeCallback(&cb);
}

void
f_matchfuzzy(Arr(Var) argvars, OUT Var* returnVar) {
   do_fuzzymatch(argvars, returnVar, false);
}

void
f_matchfuzzypos(Arr(Var) argvars, OUT Var* returnVar) {
    do_fuzzymatch(argvars, returnVar, true);
}

//Same as fuzzy_match_item_compare() except for use with a string match
private int
fuzzyMatchStr_compare(const void *s0, const void *s1) {
   int score0 = ((FuzzyMatch *)s0)->score;
   int score1 = ((FuzzyMatch *)s1)->score;
   int idx0 = ((FuzzyMatch *)s0)->idx;
   int idx1 = ((FuzzyMatch *)s1)->idx;

   if (score0 == score1)
      return idx0 == idx1 ? 0 : idx0 > idx1 ? 1 : -1;
   else
      return score0 > score1 ? -1 : 1;
}

//Sort fuzzy matches by score
void
fuzzySortByScore(OUT Fuzzy* fuzzy) {
   // Sort the list by the descending order of the match score
   qsort((void *)fuzzy->c, (Unt)fuzzy->len, sizeof(FuzzyMatch), fuzzyMatchStr_compare);
}

//Same as fuzzy_match_item_compare() except for use with a function name
//string match. <SNR> functions should be sorted to the end.
private int
fuzzy_match_func_compare(const void *s1, const void *s2) {
   int      v1 = ((FuzzyMatch *)s1)->score;
   int      v2 = ((FuzzyMatch *)s2)->score;
   int      idx1 = ((FuzzyMatch *)s1)->idx;
   int      idx2 = ((FuzzyMatch *)s2)->idx;
   CS str1 = ((FuzzyMatch *)s1)->str;
   CS str2 = ((FuzzyMatch *)s2)->str;

   if (*str1 != '<' && *str2 == '<')
      return -1;
   if (*str1 == '<' && *str2 != '<')
      return 1;
   if (v1 == v2)
      return idx1 == idx2 ? 0 : idx1 > idx2 ? 1 : -1;
   else
      return v1 > v2 ? -1 : 1;
}

//Sort fuzzy matches of function names by score. <SNR> functions should be sorted to the end.
private void
sortFnNamesByScore(Arr(FuzzyMatch) fm, int sz) {
   // Sort the list by the descending order of the match score
   qsort((void *)fm, (Unt)sz, sizeof(FuzzyMatch), fuzzy_match_func_compare);
}

//Fuzzy match 'pat' in 'str'. Return 0 if there is no match. Otherwise, return the match score.
int
fuzzyMatchStr(CS str, CS pat) {
   int      score = FUZZY_SCORE_NONE;
   Unt   matchpos[FUZZY_MATCH_MAX_LEN];

   if (str == NULL || pat == NULL)
      return score;

   fuzzy_match(str, pat, true, &score, matchpos, sizeof(matchpos) / sizeof(matchpos[0]));

   return score;
}

//Fuzzy match the position of string 'pat' in string 'str'.
//Return a dynamic array of matching positions. If there is no match, return NULL.
ArrayList *
fuzzyMatchStr_with_pos(CS str, CS pat) {
   int          score = FUZZY_SCORE_NONE;
   ArrayList       *match_positions = NULL;
   Unt       matches[FUZZY_MATCH_MAX_LEN];
   int          j = 0;

   if (str == NULL || pat == NULL)
      return NULL;

   match_positions = ALLOC_ONE(ArrayList);
   if (match_positions == NULL)
      return NULL;
   ga_init2(match_positions, sizeof(Unt), 10);

   if (!fuzzy_match(str, pat, false, &score, matches, FUZZY_MATCH_MAX_LEN)
          || score == FUZZY_SCORE_NONE) {
      ga_clear(match_positions);
      eeglFree(match_positions);
      return NULL;
   }

   for (Byte *p = pat; *p != ZERO; MB_PTR_ADV(p)) {
      if (!SPACE_OR_TAB(mb_ptr2char(p))) {
         ga_grow(match_positions, 1);
         ((Unt *)match_positions->c)[match_positions->len] = matches[j];
         match_positions->len++;
         j++;
      }
   }

   return match_positions;
}

// Find the end of the word. Assumes it starts inside a word. Return a pointer to after the word
CS
find_word_end(CS ptr) {
   int start_class = mb_get_class(ptr);
   if (start_class > 1) {
      while (*ptr != ZERO) {
         ptr += utfCharLen(ptr);
         if (mb_get_class(ptr) != start_class)
            break;
      }
   } 
   return ptr;
}


// Find the end of the line, omitting CR and NL at the end. Returns a pointer to just after the line.
CS
find_line_end(CS ptr) {
   CS s = ptr + STRLEN(ptr);
   while (s > ptr && (s[-1] == ENTER || s[-1] == NL))
      --s;
   return s;
}

//This function splits the line pointed to by `*ptr` into words and performs
//a fuzzy match for the pattern `pat` on each word. It iterates through the
//line, moving `*ptr` to the start of each word during the process.
//
//If a match is found:
//- `*ptr` points to the start of the matched word.
//- `*len` is set to the length of the matched word.
//- `*score` contains the match score.
//
//If no match is found, `*ptr` is updated to the end of the line.
int
fuzzyMatchStr_in_line(
   Byte   **ptr,
   CS pat,
   int* len,
   Pos* current_pos,
   int* score)
{
   CS str = *ptr;
   CS strBegin = str;
   CS end = NULL;
   CS start = NULL;
   int found = false;
   Byte save_end;
   CS line_end = NULL;

   if (!str || !pat)
      return found;
   line_end = find_line_end(str);

   while (str < line_end) {
      // Skip non-word characters
      start = findWordStart(str);
      if (*start == ZERO)
          break;
      end = find_word_end(start);

      // Extract the word from start to end
      save_end = *end;
      *end = ZERO;

      // Perform fuzzy match
      *score = fuzzyMatchStr(start, pat);
      *end = save_end;

      if (*score != FUZZY_SCORE_NONE) {
         *len = (int)(end - start);
         found = true;
         *ptr = start;
         if (current_pos)
            current_pos->col += (int)(end - strBegin);
         break;
      }

      // Move to the end of the current word for the next iteration
      str = end;
      // Ensure we continue searching after the current word
      while (*str != ZERO && !eeIsWordPtr(str))
         MB_PTR_ADV(str);
   }

   if (!found)
      *ptr = line_end;

   return found;
}

//Search for the next fuzzy match in the specified buffer. Attempt to find the next occurrence of 
//the given pattern in the buffer, starting from the current position. Handle line wrapping and 
//direction of search. Return true if a match is found, otherwise false.
int
search_for_fuzzy_match(
   Book* book,
   Pos* pos,
   CS pattern,
   int dir,
   Pos* start_pos,
   OUT int* len,
   OUT CS* ptr,
   int* score
) {
   Pos current_pos = *pos;
   Pos circly_end;
   int found_new_match = false;
   int looped_around = false;
   int whole_line = ctrl_x_mode_whole_line();

   if (book == curBook)
      circly_end = *start_pos;
   else {
      circly_end.lnum = book->mem.lineCount;
      circly_end.col = 0;
      circly_end.coladd = 0;
   }

   if (whole_line && start_pos->lnum != pos->lnum)
      current_pos.lnum += dir;

   do {

      // Check if looped around and back to start position
      if (looped_around && EQUAL_POS(current_pos, circly_end))
         break;

      // Ensure current_pos is valid
      if (current_pos.lnum >= 1 && current_pos.lnum <= book->mem.lineCount) {
         // Get the current line buffer
         *ptr = memGetLine(book, current_pos.lnum, false);
         if (!whole_line)
            *ptr += current_pos.col;

         // If ptr is end of line is reached, move to next line
         // or previous line based on direction
         if (*ptr != NULL && **ptr != ZERO) {
            if (!whole_line) {
               // Try to find a fuzzy match in the current line starting from current position
               found_new_match = fuzzyMatchStr_in_line(ptr, pattern, len, &current_pos, score);
               if (found_new_match) {
                  *pos = current_pos;
                  break;
               } ei (looped_around && current_pos.lnum == circly_end.lnum)
                  break;
            } else {
               if (fuzzyMatchStr(*ptr, pattern) != FUZZY_SCORE_NONE) {
                  found_new_match = true;
                  *pos = current_pos;
                  *len = (int)memGetBookLen(book, current_pos.lnum);
                  break;
               }
            }
         }
      }

      // Move to the next line or previous line based on direction
      if (dir == FORWARD) {
         if (++current_pos.lnum > book->mem.lineCount) {
            if (wrapSearchG) {
               current_pos.lnum = 1;
               looped_around = true;
            } else
               break;
         }
      } else {
         if (--current_pos.lnum < 1) {
            if (wrapSearchG) {
               current_pos.lnum = book->mem.lineCount;
               looped_around = true;
            } else
               break;
         }
      }
      current_pos.col = 0;
   } while (true);

   return found_new_match;
}

//Free an array of fuzzy string matches "fuzmatch[count]".
void
fuzmatch_str_free(FuzzyMatch *fuzmatch, int count) {
   if (!fuzmatch)
      return;

   for (int i = 0; i < count; ++i)
      eeglFree(fuzmatch[i].str);
   eeglFree(fuzmatch);
}

//Copy a list of fuzzy matches into a string list after sorting the matches by
//the fuzzy score. Free the memory allocated for 'fuzzy'.
//Return OK on success and FAIL on memory allocation failure.
int
defuzz(
   OUT ExpandMatch* matches,
   Fuzzy fuzzy,
   Boole funcsort
) {
   Unt const len = fuzzy.len;
   if (fuzzy.len == 0)
      goto theend;

   if (matches->cap < len) {
      matches->c = allocateArray(len, CS, matches->a);
      matches->cap = len;
   }

   // Sort the list by the descending order of the match score
   if (funcsort)
      sortFnNamesByScore((void *)fuzzy.c, len);
   else
      fuzzySortByScore(&fuzzy);

   for (Unt i = 0; i < len; i++)
      matches->c[i] = fuzzy.c[i].str;
   
theend:
   matches->len = len;
   return OK;
}

//Fuzzy match algorithm ported from https://github.com/jhawthorn/fzy.
//This implementation extends the original by supporting multibyte characters.

#define MATCH_MAX_LEN FUZZY_MATCH_MAX_LEN

#define SCORE_GAP_LEADING -0.005
#define SCORE_GAP_TRAILING -0.005
#define SCORE_GAP_INNER -0.01
#define SCORE_MATCH_CONSECUTIVE 1.0
#define SCORE_MATCH_SLASH 0.9
#define SCORE_MATCH_WORD 0.8
#define SCORE_MATCH_CAPITAL 0.7
#define SCORE_MATCH_DOT 0.6

private int
has_match(Byte *needle, Byte *haystack) {
   while (*needle != ZERO) {
      int n_char = mb_ptr2char(needle);
      Byte *p = haystack;
      int h_char;
      int matched = false;

      while (*p != ZERO) {
         h_char = mb_ptr2char(p);

         if (n_char == h_char || MB_TOUPPER(n_char) == h_char) {
            matched = true;
            break;
         }
         p += utfCharLen(p);
      }

      if (!matched)
         return 0;

      needle += utfCharLen(needle);
      haystack = p + utfCharLen(p);
   }
   return 1;
}

typedef struct match_struct {
   int needle_len;
   int haystack_len;
   int lower_needle[MATCH_MAX_LEN];     // stores codepoints
   int lower_haystack[MATCH_MAX_LEN];   // stores codepoints
   double match_bonus[MATCH_MAX_LEN];
} match_struct;

#define IS_WORD_SEP(c) ((c) == '-' || (c) == '_' || (c) == ' ')
#define IS_PATH_SEP(c) ((c) == '/')
#define IS_DOT(c)      ((c) == '.')

private double
compute_bonus_codepoint(Unt last_c, Unt c) {
   if (ASCII_ISALNUM(c) || eeIsWordc(c)) {
      if (IS_PATH_SEP(last_c))
         return SCORE_MATCH_SLASH;
      if (IS_WORD_SEP(last_c))
         return SCORE_MATCH_WORD;
      if (IS_DOT(last_c))
         return SCORE_MATCH_DOT;
      if (MB_ISUPPER(c) && MB_ISLOWER(last_c))
         return SCORE_MATCH_CAPITAL;
   }
   return 0;
}

private void
setup_match_struct(match_struct *match, CS needle, CS haystack) {
   int i = 0;
   CS p = needle;
   while (*p != ZERO && i < MATCH_MAX_LEN) {
      Unt c = mb_ptr2char(p);
      match->lower_needle[i++] = MB_TOLOWER(c);
      MB_PTR_ADV(p);
   }
   match->needle_len = i;

   i = 0;
   p = haystack;
   Unt prev_c = '/';
   while (*p != ZERO && i < MATCH_MAX_LEN) {
      Unt c = mb_ptr2char(p);
      match->lower_haystack[i] = MB_TOLOWER(c);
      match->match_bonus[i] = compute_bonus_codepoint(prev_c, c);
      prev_c = c;
      MB_PTR_ADV(p);
      i++;
   }
   match->haystack_len = i;
}

private inline void
match_row(match_struct const* match, int row, double* curr_D,
   double* curr_M, double const* last_D, double const* last_M
) {
   int n = match->needle_len;
   int m = match->haystack_len;
   int i = row;

   const int *lower_needle = match->lower_needle;
   const int *lower_haystack = match->lower_haystack;
   const double *match_bonus = match->match_bonus;

   double prev_score = SCORE_MIN;
   double gap_score = i == n - 1 ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

   // These will not be used with this value, but not all compilers see it
   double prev_M = SCORE_MIN, prev_D = SCORE_MIN;

   for (int j = 0; j < m; j++) {
      if (lower_needle[i] == lower_haystack[j]) {
         double score = SCORE_MIN;
         if (!i) {
            score = (j * SCORE_GAP_LEADING) + match_bonus[j];
         } ei (j) { /* i > 0 && j > 0*/
             score = MAX(
                prev_M + match_bonus[j],
                // consecutive match, doesn't stack with match_bonus
                prev_D + SCORE_MATCH_CONSECUTIVE);
         }
         prev_D = last_D[j];
         prev_M = last_M[j];
         curr_D[j] = score;
         curr_M[j] = prev_score = MAX(score, prev_score + gap_score);
      } else {
         prev_D = last_D[j];
         prev_M = last_M[j];
         curr_D[j] = SCORE_MIN;
         curr_M[j] = prev_score = prev_score + gap_score;
      }
    }
}

private double
match_positions(Byte *needle, Byte *haystack, Unt *positions) {
   if (!*needle)
      return SCORE_MIN;

   match_struct match;
   setup_match_struct(&match, needle, haystack);

   int n = match.needle_len;
   int m = match.haystack_len;

   if (m > MATCH_MAX_LEN || n > m) {
      // Unreasonably large candidate: return no score
      // If it is a valid match it will still be returned, it will
      // just be ranked below any reasonably sized candidates
      return SCORE_MIN;
   } ei (n == m) {
      // Since this method can only be called with a haystack which
      // matches needle. If the lengths of the strings are equal the
      // strings themselves must also be equal (ignoring case).
      if (positions) {
         for (int i = 0; i < n; i++)
            positions[i] = i;
      } 
      return SCORE_MAX;
   }

   // D[][] Stores the best score for this position ending with a match.
   // M[][] Stores the best possible score at this position.
   double (*D)[MATCH_MAX_LEN], (*M)[MATCH_MAX_LEN];
   M = alloc(sizeof(double) * MATCH_MAX_LEN * n);
   D = alloc(sizeof(double) * MATCH_MAX_LEN * n);
   if (!D)
      return SCORE_MIN;

   match_row(&match, 0, D[0], M[0], D[0], M[0]);
   for (int i = 1; i < n; i++)
      match_row(&match, i, D[i], M[i], D[i - 1], M[i - 1]);

   // backtrace to find the positions of optimal matching
   if (positions) {
      int match_required = 0;
      for (int i = n - 1, j = m - 1; i >= 0; i--) {
         for (; j >= 0; j--) {
            // There may be multiple paths which result in the optimal weight.
            //
            // For simplicity, we will pick the first one
            // we encounter, the latest in the candidate
            // string.
            if (D[i][j] != SCORE_MIN && (match_required || D[i][j] == M[i][j])) {
               // If this score was determined using SCORE_MATCH_CONSECUTIVE, the
               // previous character MUST be a match
               match_required = i && j && M[i][j] == D[i - 1][j - 1] + SCORE_MATCH_CONSECUTIVE;
               positions[i] = j--;
               break;
            }
         }
      }
   }

   double result = M[n - 1][m - 1];

   eeglFree(M);
   eeglFree(D);

   return result;
}

//}}}
//{{{string functions

#define MAX_ALLOWED_STRING_WIDTH 1048576    // 1 MiB

//Make a Var of the first character of "input" and store it in "output". Return OK or FAIL.
private int
copy_first_char_to_tv(CS input, Var* output) {
   Byte buf[MB_MAXBYTES + 1];

   if (input == NULL || output == NULL)
      return FAIL;

   int len = utfCharLen(input);
   STRNCPY(buf, input, len);
   buf[len] = ZERO;
   output->tag = VAR_STRING;
   output->string = copyStr(buf);

   return output->string == NULL ? FAIL : OK;
}

//Implementation of map() and filter() for a String. Apply "expr" to every
//character in string "str" and return the result in "returnVar".
void
string_filter_map(
   CS str,
   FilterMap filtermap,
   Var* expr,
   Var* returnVar
) {
   CS p;
   Var   tv;
   ArrayList   ga;
   int len = 0;
   int idx = 0;
   int rem;
   Var newtv;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   // set_EeglVar_nr() doesn't set the type
   set_EeglVar_type(VV_KEY, VAR_NUMBER);

   ga_init2(&ga, sizeof(char), 80);
   for (p = str; *p != ZERO; p += len) {
      if (copy_first_char_to_tv(p, &tv) == FAIL)
         break;
      len = (int)STRLEN(tv.string);

      set_EeglVar_nr(VV_KEY, idx);
      if (filter_map_one(&tv, expr, filtermap, &newtv, &rem) == FAIL || anyEmsgG) {
         clearVar(&newtv);
         clearVar(&tv);
         break;
      }
      if (filtermap == FILTERMAP_MAP || filtermap == FILTERMAP_MAPNEW) {
         if (newtv.tag != VAR_STRING) {
            clearVar(&newtv);
            clearVar(&tv);
            emsg(_(e_string_required));
            break;
         } else
            ga_concat(&ga, newtv.string);
      }
      ei (filtermap == FILTERMAP_FOREACH || !rem)
         ga_concat(&ga, tv.string);

      clearVar(&newtv);
      clearVar(&tv);

      ++idx;
   }
   ga_append(&ga, ZERO);
   returnVar->string = ga.c;
}

//Implementation of reduce() for String "argvars[0]" using the function "expr"
//starting with the optional initial value "argvars[2]" and return the result in "returnVar".
private void
string_reduce(Var* argvars, Var* expr, Var* returnVar) {
   CS p = tv_get_string(&argvars[0]);
   int len;
   Var  argv[3];
   int r;
   int called_emsg_start = called_emsg;

   if (argvars[2].tag == VAR_UNKNOWN) {
      if (*p == ZERO) {
         showErrFmtMsg(_(e_reduce_of_an_empty_str_with_no_initial_value), "String");
         return;
      }
      if (copy_first_char_to_tv(p, returnVar) == FAIL)
         return;
      p += STRLEN(returnVar->string);
   } ei (check_for_string_arg(argvars, 2) == FAIL)
      return;
   else
      copy_tv(OUT returnVar, &argvars[2]);

   for ( ; *p != ZERO; p += len) {
      argv[0] = *returnVar;
      if (copy_first_char_to_tv(p, &argv[1]) == FAIL)
         break;
      len = (int)STRLEN(argv[1].string);

      r = eval_expr_typval(expr, true, argv, 2, returnVar);

      clearVar(&argv[0]);
      clearVar(&argv[1]);
      if (r == FAIL || called_emsg != called_emsg_start)
          return;
   }
}

// Implementation of "byteidx()" and "byteidxcomp()" functions
private void
byteidx_common(Var* argvars, Var* returnVar, Boole comp) {
   returnVar->number = -1;

   CS str = convertVarToStringSingleUse(&argvars[0]);
   Long idx = varGetNumberChk(argvars + 1, NULL);
   if (!str || idx < 0)
      return;

   Long   utf16idx = false;
   if (argvars[2].tag != VAR_UNKNOWN) {
      Boole error = false;
      utf16idx = varGetNumberChk(argvars + 2, OUT &error);
      if (error)
         return;
      if (utf16idx < 0 || utf16idx > 1) {
         showErrFmtMsg(_(e_using_number_as_bool_nr), utf16idx);
         return;
      }
   }

   Unt (*ptr2len)(Byte*);
   if (comp)
      ptr2len = utf_ptr2len;
   else
      ptr2len = utfCharLen;

   CS t = str;
   for ( ; idx > 0; idx--) {
      if (*t == ZERO)      // EOL reached
         return;
      if (utf16idx) {
         int clen = ptr2len(t);
         int c = (clen > 1) ? mb_ptr2char(t) : *t;
         if (c > 0xFFFF)
            idx--;
      }
      if (idx > 0)
         t += ptr2len(t);
   }
   returnVar->number = (Long)(t - str);
}

void
f_byteidx(Arr(Var) argvars, OUT Var* returnVar) {
   byteidx_common(argvars, returnVar, false);
}

void
f_byteidxcomp(Var* argvars, Var* returnVar) {
   byteidx_common(argvars, returnVar, true);
}

void
f_charidx(Var* argvars, Var* returnVar) {
   returnVar->number = -1;

   if (check_for_string_arg(argvars, 0) == FAIL
         || check_for_number_arg(argvars, 1) == FAIL
         || check_for_opt_bool_arg(argvars, 2) == FAIL
         || (argvars[2].tag != VAR_UNKNOWN && check_for_opt_bool_arg(argvars, 3) == FAIL))
      return;

   CS str = convertVarToStringSingleUse(&argvars[0]);
   Long idx = varGetNumberChk(argvars + 1, NULL);
   if (str == NULL || idx < 0)
      return;

   Long countcc = false;
   Long utf16idx = false;
   if (argvars[2].tag != VAR_UNKNOWN) {
      countcc = tv_get_bool(&argvars[2]);
      if (argvars[3].tag != VAR_UNKNOWN)
         utf16idx = tv_get_bool(&argvars[3]);
   }

   Unt (*ptr2len)(CS);
   if (countcc)
      ptr2len = utf_ptr2len;
   else
      ptr2len = utfCharLen;

   Unt len = 0;
   for (CS p = str; utf16idx ? idx >= 0 : p <= str + idx; len++) {
      if (*p == ZERO) {
         // If the index is exactly the number of bytes or utf-16 code units
         // in the string then return the length of the string in characters.
         if (utf16idx ? (idx == 0) : (p == (str + idx)))
            returnVar->number = len;
         return;
      }
      if (utf16idx) {
         idx--;
         int clen = ptr2len(p);
         int c = (clen > 1) ? mb_ptr2char(p) : *p;
         if (c > 0xFFFF)
            idx--;
      }
      p += ptr2len(p);
   }

   returnVar->number = len > 0 ? len - 1 : 0;
}

void
f_str2list(Arr(Var) argvars, OUT Var* returnVar) {
   allocReturnList(returnVar);

   CS p = tv_get_string(&argvars[0]);

   for ( ; *p != ZERO; p += utf_ptr2len(p))
      list_append_number(returnVar->list, mb_ptr2char(p));
}

void
f_str2nr(Var* argvars, Var* returnVar) {
   int      base = 10;
   Long   n;
   int      what = 0;

   if (argvars[1].tag != VAR_UNKNOWN) {
      base = (int)tv_get_number(&argvars[1]);
      if (base != 2 && base != 10 && base != 16) {
         emsg(_(e_invalid_argument));
         return;
      }
      if (argvars[2].tag != VAR_UNKNOWN && tv_get_bool(&argvars[2]))
         what |= STR2NR_QUOTE;
   }

   CS p = skipwhite(tv_get_string_strict(&argvars[0]));
   Boole isneg = (*p == '-');
   if (*p == '+' || *p == '-')
      p = skipwhite(p + 1);
      
   switch (base) {
   case 2: what |= STR2NR_BIN + STR2NR_FORCE; break;
   case 16: what |= STR2NR_HEX + STR2NR_FORCE; break;
   }
   readLongNumber(p, NULL, NULL, what, &n, NULL, 0, false, NULL);
   // Text after the number is silently ignored.
   returnVar->number = isneg ? -n : n;
}

void
f_strgetchar(Var* argvars, Var* returnVar) {
   Boole error = false;
   int      byteidx = 0;
   returnVar->number = -1;
   CS str = convertVarToStringSingleUse(&argvars[0]);
   if (!str)
      return;
      
   int len = (int)STRLEN(str);
   int charidx = (int)varGetNumberChk(argvars + 1, OUT &error);
   if (error)
      return;

   while (charidx >= 0 && byteidx < len) {
      if (charidx == 0) {
         returnVar->number = mb_ptr2char(str + byteidx);
         break;
      }
      --charidx;
      byteidx += MB_CPTR2LEN(str + byteidx);
   }
}

void
f_stridx(Var* argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   int start_idx;

   CS needle = convertVarToStringSingleUse(&argvars[1]);
   CS haystack = convertVarToString(&argvars[0], buf);
   CS save_haystack = haystack;
   returnVar->number = -1;
   if (needle == NULL || haystack == NULL)
      return;      // type error; errmsg already given

   if (argvars[2].tag != VAR_UNKNOWN) {
      Boole error = false;

      start_idx = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (error || start_idx >= (int)STRLEN(haystack))
         return;
      if (start_idx >= 0)
         haystack += start_idx;
   }

   CS pos   = (CS)strstr((char *)haystack, (char *)needle);
   if (pos)
      returnVar->number = (Long)(pos - save_haystack);
}

void
f_string(Arr(Var) argvars, OUT Var* returnVar) {
   CS tofree;
   Byte numBuf[NUMBUFLEN];

   returnVar->tag = VAR_STRING;
   returnVar->string = tv2string(&argvars[0], &tofree, numBuf, get_copyID());
   // Make a copy if we have a value but it's not in allocated memory.
   if (returnVar->string != NULL && tofree == NULL)
      returnVar->string = copyStr(returnVar->string);
}

void
f_strlen(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->number = (Long)(STRLEN(tv_get_string(&argvars[0])));
}

private void
strchar_common(Arr(Var) argvars, OUT Var* returnVar, int skipcc) {
   CS s = tv_get_string(&argvars[0]);
   Long len = 0;
   
   Unt (*func_strAdvanceMultibyte)(OUT CS* pp);
   func_strAdvanceMultibyte = skipcc ? strAdvanceMultibyte : mb_cptr2char_adv;
   
   while (*s != ZERO) {
      func_strAdvanceMultibyte(&s);
      ++len;
   }
   returnVar->number = len;
}

void
f_strcharlen(Arr(Var) argvars, OUT Var* returnVar) {
   strchar_common(argvars, returnVar, true);
}

void
f_strchars(Arr(Var) argvars, OUT Var* returnVar) {
   Long      skipcc = false;
   if (argvars[1].tag != VAR_UNKNOWN) {
      Boole error = false;
      skipcc = varGetNumberChk(argvars + 1, OUT &error);
      if (error)
         return;
      if (skipcc < 0 || skipcc > 1) {
         showErrFmtMsg(_(e_using_number_as_bool_nr), skipcc);
         return;
      }
   }

   strchar_common(argvars, returnVar, skipcc);
}

void
f_strdisplaywidth(Arr(Var) argvars, OUT Var* returnVar) {
   int col = 0;
   returnVar->number = -1;

   CS s = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN)
      col = (int)tv_get_number(&argvars[1]);

   returnVar->number = (Long)(linetabsize_col(col, s) - col);
}

void
f_strwidth(Var* argvars, OUT Var* returnVar) {
   CS s = tv_get_string_strict(argvars);
   returnVar->number = (Long)(mb_string2cells(s, -1));
}

void
f_strcharpart(Arr(Var) argvars, OUT Var* returnVar) {
   int nbyte = 0;
   int skipcc = false;
   int len = 0;
   Boole error = false;

   CS p = tv_get_string(&argvars[0]);
   int slen = (int)STRLEN(p);

   int nchar = (int)varGetNumberChk(argvars + 1, OUT &error);
   if (!error) {
      if (argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN) {
         skipcc = varGetNumberChk(argvars + 3, OUT &error);
         if (error)
            return;
         if (skipcc < 0 || skipcc > 1) {
            showErrFmtMsg(_(e_using_number_as_bool_nr), skipcc);
            return;
         }
      }

      if (nchar > 0) {
         while (nchar > 0 && nbyte < slen) {
            nbyte += skipcc ? utfCharLen(p + nbyte) : MB_CPTR2LEN(p + nbyte);
            --nchar;
         }
      } else
         nbyte = nchar;
      if (argvars[2].tag != VAR_UNKNOWN) {
         int charlen = (int)tv_get_number(&argvars[2]);
         while (charlen > 0 && nbyte + len < slen) {
            int off = nbyte + len;

            len += (off < 0) ? 1 : (skipcc ? utfCharLen(p + off) : MB_CPTR2LEN(p + off));
            --charlen;
         }
      } else
          len = slen - nbyte;    // default: all bytes that are available.
   }

   // Only return the overlap between the specified part and the actual string.
   if (nbyte < 0) {
      len += nbyte;
      nbyte = 0;
   } ei (nbyte > slen)
      nbyte = slen;
   if (len < 0)
      len = 0;
   ei (nbyte + len > slen)
      len = slen - nbyte;

   returnVar->tag = VAR_STRING;
   returnVar->string = copySubstr(p + nbyte, len);
}

void
f_strpart(Arr(Var) argvars, OUT Var* returnVar) {
   Boole error = false;

   CS p = tv_get_string(&argvars[0]);
   int slen = (int)STRLEN(p);

   int n = (int)varGetNumberChk(argvars + 1, OUT &error);
   int len;
   if (error)
      len = 0;
   ei (argvars[2].tag != VAR_UNKNOWN)
      len = (int)tv_get_number(&argvars[2]);
   else
      len = slen - n;       // default len: all bytes that are available.

   // Only return the overlap between the specified part and the actual string.
   if (n < 0) {
      len += n;
      n = 0;
   } ei (n > slen)
      n = slen;
   if (len < 0)
      len = 0;
   ei (n + len > slen)
      len = slen - n;

   if (argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN) {
      // length in characters
      int off;
      for (off = n; off < slen && len > 0; --len)
         off += utfCharLen(p + off);
      len = off - n;
   }

   returnVar->tag = VAR_STRING;
   returnVar->string = copySubstr(p + n, len);
}

void
f_strridx(Arr(Var) argvars, OUT Var* returnVar) {
   Byte buf[NUMBUFLEN];

   CS needle = convertVarToStringSingleUse(&argvars[1]);
   CS haystack = convertVarToString(&argvars[0], buf);

   returnVar->number = -1;
   if (needle == NULL || haystack == NULL)
      return;      // type error; errmsg already given

   int haystack_len = (int)STRLEN(haystack);
   int endInd;
   if (argvars[2].tag != VAR_UNKNOWN) {
      // Third argument: upper limit for index
      endInd = (int)varGetNumberChk(argvars + 2, NULL);
      if (endInd < 0)
          return;   // can never find a match
   } else
      endInd = haystack_len;

   CS lastmatch = NULL;
   if (*needle == ZERO) {
      // Empty string matches past the end.
      lastmatch = haystack + endInd;
   } else {
      for (CS rest = haystack; *rest != '\0'; ++rest) {
         rest = STRSTR(rest, needle);
         if (rest == NULL || rest > haystack + endInd)
            break;
         lastmatch = rest;
      }
   }

   returnVar->number = lastmatch ? (Long)(lastmatch - haystack) : -1;
}

void
f_strtrans(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = sanitizeStr(tv_get_string(&argvars[0]));
}

// "tolower(string)" function
void
f_tolower(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = strlow_save(tv_get_string(&argvars[0]));
}

// "toupper(string)" function
void
f_toupper(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = strup_save(tv_get_string(&argvars[0]));
}

// "tr(string, fromstr, tostr)" function
void
f_tr(Arr(Var) argvars, OUT Var* returnVar) {
   CS p;
   int  first = true;
   Byte buf[NUMBUFLEN];
   Byte buffer1[NUMBUFLEN];

   CS in_str = tv_get_string(&argvars[0]);
   CS fromstr = convertVarToString(&argvars[1], buf);
   CS tostr = convertVarToString(&argvars[2], buffer1);

   // Default return value: empty string.
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
   if (fromstr == NULL || tostr == NULL)
      return;      // type error; errmsg already given
      
   ArrayList   ga;
   ga_init2(&ga, sizeof(char), 80);

   // fromstr and tostr have to contain the same number of chars
   while (*in_str != ZERO) {
      int inlen = utfCharLen(in_str);
      CS cpstr = in_str;
      int cplen = inlen;
      int idx = 0;
      int fromlen;
      int tolen;
      for (p = fromstr; *p != ZERO; p += fromlen) {
         fromlen = utfCharLen(p);
         if (fromlen == inlen && STRNCMP(in_str, p, inlen) == 0) {
            for (p = tostr; *p != ZERO; p += tolen) {
               tolen = utfCharLen(p);
               if (idx-- == 0) {
                   cplen = tolen;
                   cpstr = p;
                   break;
               }
            }
            if (*p == ZERO)   // tostr is shorter than fromstr
               goto error;
            break;
         }
         ++idx;
      }

      if (first && cpstr == in_str) {
         // Check that fromstr and tostr have the same number of
         // (multi-byte) characters.  Done only once when a character
         // of in_str doesn't appear in fromstr.
         first = false;
         for (p = tostr; *p != ZERO; p += tolen) {
            tolen = utfCharLen(p);
            --idx;
         }
         if (idx != 0)
            goto error;
      }

      (void)ga_grow(&ga, cplen);
      MEMMOVE((char *)ga.c + ga.len, cpstr, (Unt)cplen);
      ga.len += cplen;

      in_str += inlen;
   }

   // add a terminating ZERO
   (void)ga_grow(&ga, 1);
   ga_append(&ga, ZERO);

   returnVar->string = ga.c;
   
   return;   
   
error:
   showErrFmtMsg(_(e_invalid_argument_str), fromstr);
   ga_clear(&ga);
   return;
}

// "trim({expr})" function
void
f_trim(Arr(Var) argvars, OUT Var* returnVar) {
   Byte buffer0[NUMBUFLEN];
   Byte buffer1[NUMBUFLEN];
   CS mask = NULL;
   CS p;
   int dir = 0;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS head = convertVarToString(&argvars[0], buffer0);
   if (!head)
      return;

   if (check_for_opt_string_arg(argvars, 1) == FAIL)
      return;

    if (argvars[1].tag == VAR_STRING) {
      mask = convertVarToString(&argvars[1], buffer1);
      if (*mask == ZERO)
         mask = NULL;

      if (argvars[2].tag != VAR_UNKNOWN) {
         Boole error = false;

         // leading or trailing characters to trim
         dir = (int)varGetNumberChk(argvars + 2, OUT &error);
         if (error)
            return;
         if (dir < 0 || dir > 2) {
            showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[2]));
            return;
         }
      }
   }

   if (dir == 0 || dir == 1) {
      // Trim leading characters
      while (*head != ZERO) {
         Unt c1 = mb_ptr2char(head);
         if (mask == NULL) {
            if (c1 > ' ' && c1 != 0xa0)
                break;
         } else {
            for (p = mask; *p != ZERO; MB_PTR_ADV(p))
               if (c1 == mb_ptr2char(p))
                  break;
            if (*p == ZERO)
                break;
         }
         MB_PTR_ADV(head);
      }
   }

   CS tail = head + STRLEN(head);
   if (dir == 0 || dir == 2) {
      // Trim trailing characters
      CS prev;
      for (; tail > head; tail = prev) {
         prev = tail;
         MB_PTR_BACK(head, prev);
         Unt c1 = mb_ptr2char(prev);
         if (mask == NULL) {
            if (c1 > ' ' && c1 != 0xa0)
                break;
         } else {
            for (p = mask; *p != ZERO; MB_PTR_ADV(p))
               if (c1 == mb_ptr2char(p))
                  break;
            if (*p == ZERO)
               break;
         }
      }
   }
   returnVar->string = copySubstr(head, tail - head);
}

//Get number argument from "idxp" entry in "tvs".  First entry is 1. Skip the entry.
private Long
tv_nr(Var* tvs, OUT int* idxp) {
   int idx = *idxp - 1;
   Long n = 0;
   Boole err = false;

   if (tvs[idx].tag == VAR_UNKNOWN)
      emsg(_(e_printf));
   else {
      ++*idxp;
      n = varGetNumberChk(tvs + idx, OUT &err);
      if (err)
          n = 0;
   }
   return n;
}

//Get string argument from "idxp" entry in "tvs". First entry is 1.
//If "tofree" is NULL convertVarToStringSingleUse() is used. Some types (e.g. List)
//are not converted to a string.
//If "tofree" is not NULL echo_string() is used. All types are converted to
//a string with the same format as ":echo". The caller must free "*tofree". NULL for an error.
private CS
tv_str(Var* tvs, int* idxp, Byte** tofree) {
   int idx = *idxp - 1;
   CS s = NULL;
   static Byte numBuf[NUMBUFLEN];

   if (tvs[idx].tag == VAR_UNKNOWN)
      emsg(_(e_printf));
   else {
      ++*idxp;
      if (tofree)
         s = echo_string(&tvs[idx], tofree, numBuf, get_copyID());
      else
         s = convertVarToStringSingleUse(&tvs[idx]);
   }
   return s;
}

//Get float argument from "idxp" entry in "tvs".  First entry is 1.
private double
tv_float(Var* tvs, int* idxp) {
   int idx = *idxp - 1;
   double f = 0;

   if (tvs[idx].tag == VAR_UNKNOWN)
      emsg(_(e_printf));
   else {
      ++*idxp;
      if (tvs[idx].tag == VAR_FLOAT)
         f = tvs[idx].floatt;
      ei (tvs[idx].tag == VAR_NUMBER)
         f = (double)tvs[idx].number;
      else
         emsg(_(e_expected_float_argument_for_printf));
   }
   return f;
}

private void
format_overflow_error(CS pstart) {
   Unt   arglen = 0;
   CS p = pstart;

   while (EE_ISDIGIT((int)(*p)))
      ++p;

   arglen = p - pstart;
   CS argcopy = ALLOC_CLEAR_MULT(Byte, arglen + 1);
   if (argcopy) {
      STRNCPY(argcopy, pstart, arglen);
      showErrFmtMsg(_( e_val_too_large), argcopy);
      free(argcopy);
   } else
      showErrFmtMsg(_(e_out_of_memory_allocating_nr_bytes), arglen);
}

private int
parseUnsignedInt(CS pstart, OUT CS* p, OUT Unt* uj, Boole overflow_err) {
   *uj = **p - '0';
   ++*p;

   while (EE_ISDIGIT((Unt)(**p)) && *uj < MAX_ALLOWED_STRING_WIDTH) {
      *uj = 10 * *uj + (unsigned int)(**p - '0');
      (*p)++;
   }

   if (*uj > MAX_ALLOWED_STRING_WIDTH) {
      if (overflow_err) {
         format_overflow_error(pstart);
         return FAIL;
      } else
         *uj = MAX_ALLOWED_STRING_WIDTH;
   }

   return OK;
}

enum {
   TYPE_UNKNOWN = -1,
   TYPE_INT,
   TYPE_LONGINT,
   TYPE_LONGLONGINT,
   TYPE_UNSIGNEDINT,
   TYPE_UNSIGNEDLONGINT,
   TYPE_UNSIGNEDLONGLONGINT,
   TYPE_POINTER,
   TYPE_PERCENT,
   TYPE_CHAR,
   TYPE_STRING,
   TYPE_FLOAT
};

//Types that can be used in a format string
private int
format_typeof(CS type) {
   // allowed values: \0, h, l, L
   Byte length_modifier = '\0';

   // current conversion specifier character
   Byte fmt_spec = '\0';

   // parse 'h', 'l' and 'll' length modifiers
   if (*type == 'h' || *type == 'l') {
      length_modifier = *type;
      type++;
      if (length_modifier == 'l' && *type == 'l') {
         // double l = __int64 / Long
         length_modifier = 'L';
         type++;
      }
   }
   fmt_spec = *type;

   // common synonyms:
   switch (fmt_spec) {
   case 'i': fmt_spec = 'd'; break;
   case '*': fmt_spec = 'd'; length_modifier = 'h'; break;
   case 'D': fmt_spec = 'd'; length_modifier = 'l'; break;
   case 'U': fmt_spec = 'u'; length_modifier = 'l'; break;
   case 'O': fmt_spec = 'o'; length_modifier = 'l'; break;
   default: break;
   }

   // get parameter value, do initial processing
   switch (fmt_spec) {
   // '%' and 'c' behave similar to 's' regarding flags and field
   // widths
   case '%':
      return TYPE_PERCENT;

   case 'c':
      return TYPE_CHAR;

   case 's':
   case 'S':
      return TYPE_STRING;

   case 'd': case 'u':
   case 'b': case 'B':
   case 'o':
   case 'x': case 'X':
   case 'p': {
      //NOTE: the u, b, o, x, X and p conversion specifiers
      //imply the value is unsigned;  d implies a signed value

      //0 if numeric argument is zero (or if pointer is
      //NULL for 'p'), +1 if greater than zero (or nonzero
      //for unsigned arguments), -1 if negative (unsigned argument is never negative)

      if (fmt_spec == 'p')
         return TYPE_POINTER;
      ei (fmt_spec == 'b' || fmt_spec == 'B')
         return TYPE_UNSIGNEDLONGLONGINT;
      ei (fmt_spec == 'd') {
         // signed
         switch (length_modifier) {
         case '\0':
         case 'h':
            // char and short arguments are passed as int.
            return TYPE_INT;
         case 'l':
            return TYPE_LONGINT;
         case 'L':
            return TYPE_LONGLONGINT;
         }
      } else {
         //unsigned
         switch (length_modifier) {
         case '\0':
         case 'h':
            return TYPE_UNSIGNEDINT;
         case 'l':
            return TYPE_UNSIGNEDLONGINT;
         case 'L':
            return TYPE_UNSIGNEDLONGLONGINT;
         }
      }
   }
   break;

   case 'f':
   case 'F':
   case 'e':
   case 'E':
   case 'g':
   case 'G':
      return TYPE_FLOAT;
   }

   return TYPE_UNKNOWN;
}


private CS
format_typename(CS type) {
   switch (format_typeof(type)) {
   case TYPE_INT: return _("int");
   case TYPE_LONGINT: return _("long int");
   case TYPE_LONGLONGINT: return _("long long int");
   case TYPE_UNSIGNEDINT: return _("unsigned int");
   case TYPE_UNSIGNEDLONGINT: return _("unsigned long int");
   case TYPE_UNSIGNEDLONGLONGINT: return _("unsigned long long int");
   case TYPE_POINTER: return _("pointer");
   case TYPE_PERCENT: return _("percent");
   case TYPE_CHAR: return _("char");
   case TYPE_STRING: return _("string");
   case TYPE_FLOAT: return _("float");
   default: return _("unknown");
   }
}

private int
adjust_types(OUT Byte*** ap_types, int arg, int* num_posarg, CS type) {
   if (*ap_types == NULL || *num_posarg < arg) {
      int idx;
      Arr(CS) new_types;

      if (*ap_types == NULL)
         new_types = ALLOC_CLEAR_MULT(Byte *, arg);
      else
         new_types = eeRealloc((Byte **)*ap_types, arg * sizeof(Byte *));

      for (idx = *num_posarg; idx < arg; ++idx)
         new_types[idx] = NULL;

      *ap_types = new_types;
      *num_posarg = arg;
   }

   if ((*ap_types)[arg - 1] != NULL) {
      if ((*ap_types)[arg - 1][0] == '*' || type[0] == '*') {
         CS pt = type;
         if (pt[0] == '*')
            pt = (*ap_types)[arg - 1];

         if (pt[0] != '*') {
            switch (pt[0]) {
            case 'd': case 'i': 
               break;
            default:
               showErrFmtMsg(
                     _(e_positional_num_field_spec_reused_str_str), 
                     arg, 
                     format_typename((*ap_types)[arg - 1]), format_typename(type)
               );
               return FAIL;
            }
         }
      } else {
         if (format_typeof(type) != format_typeof((*ap_types)[arg - 1])) {
            showErrFmtMsg(_( e_positional_arg_num_type_inconsistent_str_str), arg, format_typename(type), 
                  format_typename((*ap_types)[arg - 1]));
            return FAIL;
         }
      }
   }

   (*ap_types)[arg - 1] = type;

   return OK;
}


private int
parse_fmt_types(Byte*** ap_types, int* num_posarg, CS fmt, Var* tvs UNUSED) {
   Byte* p = fmt;
   CS arg = NULL;

   int any_pos = 0;
   int any_arg = 0;
   int arg_idx;

#define CHECK_POS_ARG do { \
    if (any_pos && any_arg) \
    { \
   showErrFmtMsg(_( e_cannot_mix_positional_and_non_positional_str), fmt); \
   goto error; \
    } \
} while (0);

   if (p == NULL)
      return OK;

   while (*p != ZERO) {
      if (*p != '%') {
         CS q = STRCHR(p + 1, '%');
         Unt  n = (q == NULL) ? STRLEN(p) : (Unt)(q - p);

         p += n;
      } else {
         // allowed values: \0, h, l, L
         Byte   length_modifier = '\0';

          // variable for positional arg
         int      pos_arg = -1;
         CS ptype = NULL;
         Byte* pstart = p+1;

         p++;  // skip '%'

         // First check to see if we find a positional argument specifier
         ptype = p;

         while (EE_ISDIGIT(*ptype))
            ++ptype;

         if (*ptype == '$') {
            if (*p == '0') {
               // 0 flag at the wrong place
               showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
               goto error;
            }

            // Positional argument
            Unt uj;

            if (parseUnsignedInt(pstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
               goto error;

            pos_arg = uj;

            any_pos = 1;
            CHECK_POS_ARG;

            ++p;
         }

         // parse flags
         while (*p == '0' || *p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '\'') {
            switch (*p) {
            case '0': break;
            case '-': break;
            case '+': break;
            case ' ': // If both the ' ' and '+' flags appear, the ' '
                 // flag should be ignored
                 break;
            case '#': break;
            case '\'': break;
            }
            p++;
         }
         // If the '0' and '-' flags both appear, the '0' flag should be ignored.

         // parse field width
         if (*(arg = p) == '*') {
            p++;

            if (EE_ISDIGIT((int)(*p))) {
               // Positional argument field width
               unsigned int uj;

               if (parseUnsignedInt(arg + 1, OUT &p, OUT &uj, tvs != NULL) == FAIL)
                  goto error;

               if (*p != '$') {
                  showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                  goto error;
               } else {
                  ++p;
                  any_pos = 1;
                  CHECK_POS_ARG;

                  if (adjust_types(ap_types, uj, num_posarg, arg) == FAIL)
                      goto error;
               }
            } else {
               any_arg = 1;
               CHECK_POS_ARG;
            }
         } ei (EE_ISDIGIT((int)(*p))) {
            // Unt could be wider than unsigned int; make sure we treat
            // argument like common implementations do
            CS digstart = p;
            unsigned int uj;

            if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
               goto error;

            if (*p == '$') {
               showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
               goto error;
            }
         }

         // parse precision
         if (*p == '.') {
            p++;

            if (*(arg = p) == '*') {
               p++;
               if (EE_ISDIGIT((int)(*p))) {
                  // Parse precision
                  unsigned int uj;

                  if (parseUnsignedInt(arg + 1, OUT &p, OUT &uj, tvs != NULL) == FAIL)
                     goto error;

                  if (*p == '$') {
                     any_pos = 1;
                     CHECK_POS_ARG;

                     ++p;

                     if (adjust_types(ap_types, uj, num_posarg, arg) == FAIL)
                        goto error;
                  } else {
                     showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                     goto error;
                  }
               } else {
                  any_arg = 1;
                  CHECK_POS_ARG;
               }
            } ei (EE_ISDIGIT((int)(*p))) {
               // Unt could be wider than unsigned int; make sure we
               // treat argument like common implementations do
               CS digstart = p;
               unsigned int uj;

               if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
                  goto error;

               if (*p == '$') {
                  showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                  goto error;
               }
            }
         }

         if (pos_arg != -1) {
            any_pos = 1;
            CHECK_POS_ARG;

            ptype = p;
         }

         // parse 'h', 'l' and 'll' length modifiers
         if (*p == 'h' || *p == 'l') {
            length_modifier = *p;
            p++;
            if (length_modifier == 'l' && *p == 'l') {
               // double l = __int64 / Long
               // length_modifier = 'L';
               p++;
            }
         }

         switch (*p) {
         // Check for known format specifiers. % is special!
         case 'i':
         case '*':
         case 'd':
         case 'u':
         case 'o':
         case 'D':
         case 'U':
         case 'O':
         case 'x':
         case 'X':
         case 'b':
         case 'B':
         case 'c':
         case 's':
         case 'S':
         case 'p':
         case 'f':
         case 'F':
         case 'e':
         case 'E':
         case 'g':
         case 'G':
            if (pos_arg != -1) {
               if (adjust_types(ap_types, pos_arg, num_posarg, ptype) == FAIL)
                  goto error;
            } else {
               any_arg = 1;
               CHECK_POS_ARG;
            }
            break;

         default:
            if (pos_arg != -1) {
               showErrFmtMsg(_( e_cannot_mix_positional_and_non_positional_str), fmt);
               goto error;
            }
         }

         if (*p != ZERO)
            p++;     // step over the just processed conversion specifier
      }
   }

   for (arg_idx = 0; arg_idx < *num_posarg; ++arg_idx) {
      if ((*ap_types)[arg_idx] == NULL) {
         showErrFmtMsg(_(e_fmt_arg_nr_unused_str), arg_idx + 1, fmt);
         goto error;
      }

      if (tvs && tvs[arg_idx].tag == VAR_UNKNOWN) {
         showErrFmtMsg(_(e_positional_nr_out_of_bounds_str), arg_idx + 1, fmt);
         goto error;
      }
   }

   return OK;

error:
   eeglFree((Byte**)*ap_types);
   *ap_types = NULL;
   *num_posarg = 0;
   return FAIL;
}

private void
skip_to_arg(
    Arr(CS) ap_types,
    va_list ap_start,
    va_list* ap,
    int* arg_idx,
    int* arg_cur,
    CS fmt
) {
   int arg_min = 0;

   if (*arg_cur + 1 == *arg_idx) {
      ++*arg_cur;
      ++*arg_idx;
      return;
   }

   if (*arg_cur >= *arg_idx) {
      // Reset ap to ap_start and skip arg_idx - 1 types
      va_end(*ap);
      va_copy(*ap, ap_start);
   } else {
      // Skip over any we should skip
      arg_min = *arg_cur;
   }

   for (*arg_cur = arg_min; *arg_cur < *arg_idx - 1; ++*arg_cur) {
      if (ap_types == NULL || ap_types[*arg_cur] == NULL) {
          internalErrFmtMsg(e_aptypes_is_null_nr_str, *arg_cur, fmt);
          return;
      }

      CS p = ap_types[*arg_cur];

      int fmt_type = format_typeof(p);

      // get parameter value, do initial processing
      switch (fmt_type) {
      case TYPE_PERCENT:
      case TYPE_UNKNOWN:
          break;

      case TYPE_CHAR:
          va_arg(*ap, int);
          break;

      case TYPE_STRING:
          va_arg(*ap, char *);
          break;

      case TYPE_POINTER:
          va_arg(*ap, void *);
          break;

      case TYPE_INT:
          va_arg(*ap, int);
          break;

      case TYPE_LONGINT:
          va_arg(*ap, long int);
          break;

      case TYPE_LONGLONGINT:
          va_arg(*ap, Long);
          break;

      case TYPE_UNSIGNEDINT:
          va_arg(*ap, unsigned int);
          break;

      case TYPE_UNSIGNEDLONGINT:
          va_arg(*ap, unsigned long int);
          break;

      case TYPE_UNSIGNEDLONGLONGINT:
          va_arg(*ap, Ulong);
          break;

      case TYPE_FLOAT:
          va_arg(*ap, double);
          break;
      }
   }

   //Because we know that after we return from this call, a va_arg() call is made, we can 
   //pre-emptively increment the current argument index.
   ++*arg_cur;
   ++*arg_idx;

   return;
}


//Return the representation of infinity for printf() function:
//"-inf", "inf", "+inf", " inf", "-INF", "INF", "+INF" or " INF".
private CS
infinity_str(Unt positive, char fmt_spec, int force_sign, int space_for_positive) {
   static CS table[] = {SMAP((CS),
      "-inf", "inf", "+inf", " inf",
      "-INF", "INF", "+INF", " INF"
   )};
   int idx = positive * (1 + force_sign + force_sign * space_for_positive);

   if (ASCII_ISUPPER(fmt_spec))
      idx += 4;
   return table[idx];
}

int
eeVarPrintf0(
   CS str,
   Unt str_m,
   char const* fmt,
   va_list ap_start,
   Var* tvs
) {
   Unt str_l = 0; // number of formatted characters. That is, the number of characters that 
                  // would have been written to the string buf if it were large enough.

   CS p = (CS)fmt;
   int arg_cur = 0;
   int num_posarg = 0;
   int arg_idx = 1;
   va_list ap;
   Byte** ap_types = NULL;

   if (parse_fmt_types(&ap_types, &num_posarg, (CS)fmt, tvs) == FAIL)
      return 0;

   va_copy(ap, ap_start);

   if (!p)
      p = S"";
   while (*p != ZERO) {
      if (*p != '%') {
         CS q = STRCHR(p + 1, '%');
         Unt  n = (q == NULL) ? STRLEN(p) : (Unt)(q - p);

         // Copy up to the next '%' or ZERO without any changes.
         if (str_l < str_m) {
            Unt avail = str_m - str_l;
            MEMMOVE(str + str_l, p, n > avail ? avail : n);
         }
         p += n;
         str_l += n;
      } else {
         Unt  min_field_width = 0, precision = 0;
         int       zero_padding = 0, precision_specified = 0, justify_left = 0;
         int       alternate_form = 0, force_sign = 0;

         // If both the ' ' and '+' flags appear, the ' ' flag should be ignored.
         int space_for_positive = 1;

         // allowed values: \0, h, l, L
         char length_modifier = '\0';

         // temporary buffer for simple numeric->string conversion
# define TMP_LEN 350   // On my system 1e308 is the biggest number possible.
         // That sounds reasonable to use as the maximum printable.
         Byte tmp[TMP_LEN];

         // string address in case of string argument
         CS str_arg = NULL;

         // natural field width of arg without padding and sign
         Unt str_arg_l;

         // unsigned char argument value - only defined for c conversion.
         // N.B. standard explicitly states the char argument for the c conversion is unsigned
         unsigned char uchar_arg;

         // number of zeros to be inserted for numeric conversions as
         // required by the precision or minimal field width
         Unt number_of_zeros_to_pad = 0;

         // index into tmp where zero padding is to be inserted
         Unt zero_padding_insertion_ind = 0;

         // current conversion specifier character
         Byte fmt_spec = '\0';

         // buffer for 's' and 'S' specs
         Byte* tofree = NULL;

         // variables for positional arg
         int pos_arg = -1;
         CS ptype;


         p++;  // skip '%'

         // First check to see if we find a positional argument specifier
         ptype = p;

         while (EE_ISDIGIT(*ptype))
            ++ptype;

         if (*ptype == '$') {
            // Positional argument
            CS digstart = p;
            unsigned int uj;

            if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
               goto error;

            pos_arg = uj;

            ++p;
         }

         // parse flags
         while (*p == '0' || *p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '\'') {
            switch (*p) {
            case '0': zero_padding = 1; break;
            case '-': justify_left = 1; break;
            case '+': force_sign = 1; space_for_positive = 0; break;
            case ' ': force_sign = 1;
               // If both the ' ' and '+' flags appear, the ' ' flag should be ignored
               break;
            case '#': alternate_form = 1; break;
            case '\'': break;
            }
            p++;
         }
         // If the '0' and '-' flags both appear, the '0' flag should be ignored.

         // parse field width
         if (*p == '*') {
            int j;
            CS digstart = p + 1;

            p++;

            if (EE_ISDIGIT((int)(*p))) {
               // Positional argument field width
               unsigned int uj;

               if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
                  goto error;

               arg_idx = uj;

               ++p;
            }

            j = tvs
               ? tv_nr(tvs, OUT &arg_idx) 
               : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt), 
                   va_arg(ap, int)
                 );

            if (j > MAX_ALLOWED_STRING_WIDTH) {
               if (tvs) {
                  format_overflow_error(digstart);
                  goto error;
               } else
                  j = MAX_ALLOWED_STRING_WIDTH;
            }

            if (j >= 0)
               min_field_width = j;
            else {
               min_field_width = -j;
               justify_left = 1;
            }
         } ei (EE_ISDIGIT((int)(*p))) {
            // Unt could be wider than unsigned int; make sure we treat
            // argument like common implementations do
            CS digstart = p;
            unsigned int uj;

            if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
               goto error;

            min_field_width = uj;
         }

         // parse precision
         if (*p == '.') {
            p++;
            precision_specified = 1;

            if (EE_ISDIGIT((int)(*p))) {
               // Unt could be wider than unsigned int; make sure we
               // treat argument like common implementations do
               CS digstart = p;
               unsigned int uj;

               if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
                  goto error;

               precision = uj;
            } ei (*p == '*') {
               int j;
               CS digstart = p;

               p++;

               if (EE_ISDIGIT((int)(*p))) {
                  // positional argument
                  unsigned int uj;

                  if (parseUnsignedInt(digstart, OUT &p, OUT &uj, tvs != NULL) == FAIL)
                     goto error;

                  arg_idx = uj;

                  ++p;
               }

               j = tvs 
                  ? tv_nr(tvs, OUT &arg_idx) 
                  : (skip_to_arg( ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt), 
                     va_arg(ap, int)
                    );

               if (j > MAX_ALLOWED_STRING_WIDTH) {
                  if (tvs) {
                     format_overflow_error(digstart);
                     goto error;
                  } else
                     j = MAX_ALLOWED_STRING_WIDTH;
               }

               if (j >= 0)
                  precision = j;
               else {
                  precision_specified = 0;
                  precision = 0;
               }
            }
         }

         // parse 'h', 'l' and 'll' length modifiers
         if (*p == 'h' || *p == 'l') {
            length_modifier = *p;
            p++;
            if (length_modifier == 'l' && *p == 'l') {
                // double l = __int64 / Long
                length_modifier = 'L';
                p++;
            }
         }
         fmt_spec = *p;

         // common synonyms:
         switch (fmt_spec) {
         case 'i': fmt_spec = 'd'; break;
         case 'D': fmt_spec = 'd'; length_modifier = 'l'; break;
         case 'U': fmt_spec = 'u'; length_modifier = 'l'; break;
         case 'O': fmt_spec = 'o'; length_modifier = 'l'; break;
         default: break;
         }

         switch (fmt_spec) {
         case 'd': case 'u': case 'o': case 'x': case 'X':
            if (tvs != NULL && length_modifier == '\0')
               length_modifier = 'L';
         }

         if (pos_arg != -1)
            arg_idx = pos_arg;

         // get parameter value, do initial processing
         switch (fmt_spec) {
         // '%' and 'c' behave similar to 's' regarding flags and field widths
         case '%':
         case 'c':
         case 's':
         case 'S':
            str_arg_l = 1;
            switch (fmt_spec) {
            case '%':
                str_arg = p;
                break;

            case 'c': {
               int j;

               j = tvs 
                  ? tv_nr(tvs, OUT &arg_idx) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                        va_arg(ap, int)
                    );

               // standard demands unsigned char
               uchar_arg = (unsigned char)j;
               str_arg = &uchar_arg;
               break;
               }

            case 's':
            case 'S':
               str_arg = tvs 
                  ? tv_str(tvs, &arg_idx, &tofree) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                    va_arg(ap, Byte *)
                    );

               if (str_arg == NULL) {
                  str_arg = S"[NULL]";
                  str_arg_l = 6;
               }
               // make sure not to address string beyond the specified precision !!!
               ei (!precision_specified)
                  str_arg_l = STRLEN(str_arg);
               // truncate string if necessary as requested by precision
               ei (precision == 0)
                  str_arg_l = 0;
               else {
                  CS q = memchr(str_arg, '\0',
                       precision <= (Unt)0x7fffffffL ? precision
                                  : (Unt)0x7fffffffL);

                  str_arg_l = (q == NULL) ? precision : (Unt)(q - str_arg);
               }
               if (fmt_spec == 'S') {
                  Byte   *p1;
                  Unt   i;
                  int   cell;

                  for (i = 0, p1 = (CS)str_arg; *p1; p1 += utfCharLen(p1)) {
                     cell = mb_ptr2cells(p1);
                     if (precision_specified && i + cell > precision)
                        break;
                     i += cell;
                  }

                  str_arg_l = p1 - (CS)str_arg;
                  if (min_field_width != 0)
                     min_field_width += str_arg_l - i;
                }
                break;

            default:
                break;
            }
         break;

         case 'd': case 'u':
         case 'b': case 'B':
         case 'o':
         case 'x': case 'X':
         case 'p': {
            //NOTE: the u, b, o, x, X and p conversion specifiers
            //imply the value is unsigned;  d implies a signed value

            //0 if numeric argument is zero (or if pointer is NULL for 'p'), +1 if greater than 
            //zero (or nonzero for unsigned arguments), -1 if negative (unsigned argument is 
            //never negative)
            int arg_sign = 0;

            //only set for length modifier h, or for no length modifiers
            int int_arg = 0;
            Unt uint_arg = 0;

            //only set for length modifier l
            long int long_arg = 0;
            unsigned long int ulong_arg = 0;

            //only set for length modifier ll
            Long llong_arg = 0;
            Ulong ullong_arg = 0;

            //only set for b conversion
            Ulong bin_arg = 0;

            //pointer argument value -only defined for p conversion
            void *ptr_arg = NULL;

            if (fmt_spec == 'p') {
               length_modifier = '\0';
               ptr_arg = tvs
                  ? (void *)tv_str(tvs, &arg_idx, NULL) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                     va_arg(ap, void *)
                    );

               if (ptr_arg != NULL)
                   arg_sign = 1;
            } ei (fmt_spec == 'b' || fmt_spec == 'B') {
               bin_arg = tvs 
                  ? (Ulong)tv_nr(tvs, OUT &arg_idx) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                     va_arg(ap, Ulong)
                    );

               if (bin_arg != 0)
                   arg_sign = 1;
            } ei (fmt_spec == 'd') {
               // signed
               switch (length_modifier) {
               case '\0':
               case 'h':
                   // char and short arguments are passed as int.
                   int_arg = tvs
                      ? tv_nr(tvs, OUT &arg_idx) 
                      : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, int)
                        );

                   if (int_arg > 0)
                  arg_sign =  1;
                   ei (int_arg < 0)
                  arg_sign = -1;
                   break;
               case 'l':
                   long_arg = tvs 
                      ? tv_nr(tvs, OUT &arg_idx) 
                      : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, long int)
                        );

                   if (long_arg > 0)
                  arg_sign =  1;
                   ei (long_arg < 0)
                  arg_sign = -1;
                   break;
               case 'L':
                   llong_arg = tvs
                      ? tv_nr(tvs, OUT &arg_idx) 
                      : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, Long)
                         );

                   if (llong_arg > 0)
                  arg_sign =  1;
                   ei (llong_arg < 0)
                  arg_sign = -1;
                   break;
               }
            } else {
               // unsigned
               switch (length_modifier) {
               case '\0':
               case 'h':
                  uint_arg = tvs
                     ? (unsigned)tv_nr(tvs, OUT &arg_idx) 
                     : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                        va_arg(ap, unsigned int)
                       );

                  if (uint_arg != 0)
                      arg_sign = 1;
                  break;
               case 'l':
                  ulong_arg = tvs
                     ? (unsigned long) tv_nr(tvs, OUT &arg_idx) 
                     : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, unsigned long int)
                       );

                  if (ulong_arg != 0)
                      arg_sign = 1;
                  break;
               case 'L':
                  ullong_arg = tvs
                     ? (Ulong) tv_nr(tvs, OUT &arg_idx) 
                     : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                        va_arg(ap, Ulong)
                       );

                  if (ullong_arg != 0)
                      arg_sign = 1;
                  break;
               }
            }

            str_arg = tmp;
            str_arg_l = 0;

            // NOTE:
            //   For d, i, u, o, x, and X conversions, if precision is
            //   specified, the '0' flag should be ignored.
            if (precision_specified)
               zero_padding = 0;
            if (fmt_spec == 'd') {
               if (force_sign && arg_sign >= 0)
                   tmp[str_arg_l++] = space_for_positive ? ' ' : '+';
               // leave negative numbers for sprintf to handle, to
               // avoid handling tricky cases like (short int)-32768
            } ei (alternate_form) {
               if (arg_sign != 0
                       && (fmt_spec == 'b' || fmt_spec == 'B'
                        || fmt_spec == 'x' || fmt_spec == 'X') )
               {
                   tmp[str_arg_l++] = '0';
                   tmp[str_arg_l++] = fmt_spec;
               }
               // alternate form should have no effect for p conversion, but ...
            }

            zero_padding_insertion_ind = str_arg_l;
            if (!precision_specified)
               precision = 1;   // default precision is 1
            if (precision == 0 && arg_sign == 0) {
               // When zero value is formatted with an explicit
               // precision 0, the resulting formatted string is
               // empty (d, i, u, b, B, o, x, X, p).
            } else {
               char   f[6];
               int   f_l = 0;

               // construct a simple format string for sprintf
               f[f_l++] = '%';
               if (!length_modifier)
                   ;
               ei (length_modifier == 'L') {
                   f[f_l++] = 'l';
                   f[f_l++] = 'l';
               }
               else
                   f[f_l++] = length_modifier;
               f[f_l++] = fmt_spec;
               f[f_l++] = '\0';

               if (fmt_spec == 'p')
                  str_arg_l += SPRINTF(tmp + str_arg_l, f, ptr_arg);
               ei (fmt_spec == 'b' || fmt_spec == 'B') {
                  Byte       b[8 * sizeof(Ulong)];
                  Unt       b_l = 0;
                  Ulong    bn = bin_arg;

                  do {
                     b[sizeof(b) - ++b_l] = '0' + (bn & 0x1);
                     bn >>= 1;
                  } while (bn != 0);

                  memcpy(tmp + str_arg_l, b + sizeof(b) - b_l, b_l);
                  str_arg_l += b_l;
               }
               ei (fmt_spec == 'd') {
                   // signed
                   switch (length_modifier) {
                   case '\0': str_arg_l += SPRINTF(tmp + str_arg_l, f, int_arg); break;
                   case 'h': str_arg_l += SPRINTF(tmp + str_arg_l, f, (Short)int_arg); break;
                   case 'l': str_arg_l += SPRINTF(tmp + str_arg_l, f, long_arg); break;
                   case 'L': str_arg_l += SPRINTF(tmp + str_arg_l, f, llong_arg); break;
                   }
               } else {
                  // unsigned
                  switch (length_modifier) {
                  case '\0': str_arg_l += SPRINTF(tmp + str_arg_l, f, uint_arg); break;
                  case 'h': str_arg_l += SPRINTF( tmp + str_arg_l, f, (Short)uint_arg); break;
                  case 'l': str_arg_l += SPRINTF(tmp + str_arg_l, f, ulong_arg); break;
                  case 'L': str_arg_l += SPRINTF(tmp + str_arg_l, f, ullong_arg); break;
                  }
               }

               // include the optional minus sign and possible "0x" in the region before the zero 
               // padding insertion point
               if (zero_padding_insertion_ind < str_arg_l && tmp[zero_padding_insertion_ind] == '-')
                  zero_padding_insertion_ind++;
               if (zero_padding_insertion_ind + 1 < str_arg_l
                     && tmp[zero_padding_insertion_ind]   == '0'
                     && (tmp[zero_padding_insertion_ind + 1] == 'x'
                         || tmp[zero_padding_insertion_ind + 1] == 'X')
               )
                  zero_padding_insertion_ind += 2;
            }

            Unt num_of_digits = str_arg_l - zero_padding_insertion_ind;

            // zero padding to specified precision?
            if (num_of_digits < precision)
                number_of_zeros_to_pad = precision - num_of_digits;
             // zero padding to specified minimal field width?
            if (!justify_left && zero_padding) {
               int n = (int)(min_field_width - (str_arg_l + number_of_zeros_to_pad));
               if (n > 0)
                  number_of_zeros_to_pad += n;
            }
            break;
         }

         case 'f':
         case 'F':
         case 'e':
         case 'E':
         case 'g':
         case 'G': {
            // Floating point.
            Byte format[40];
            int      l;
            int      remove_trailing_zeroes = false;

            double f = tvs
               ? tv_float(tvs, &arg_idx) 
               : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt), 
                  va_arg(ap, double)
                 );

            double abs_f = f < 0 ? -f : f;

            if (fmt_spec == 'g' || fmt_spec == 'G') {
               // Would be nice to use %g directly, but it prints
               // "1.0" as "1", we don't want that.
               if ((abs_f >= 0.001 && abs_f < 10000000.0)
                                 || abs_f == 0.0)
                   fmt_spec = ASCII_ISUPPER(fmt_spec) ? 'F' : 'f';
               else
                   fmt_spec = fmt_spec == 'g' ? 'e' : 'E';
               remove_trailing_zeroes = true;
            }

            if ((fmt_spec == 'f' || fmt_spec == 'F') &&
# ifdef VAX
                abs_f > 1.0e38
# else
                abs_f > 1.0e307
# endif
            ) {
               // Avoid a buffer overflow
               STRCPY(tmp, infinity_str(f > 0.0, fmt_spec, force_sign, space_for_positive));
               str_arg_l = STRLEN(tmp);
               zero_padding = 0;
            } else {
               if (isnan(f)) {
                  // Not a number: nan or NAN
                  STRCPY(tmp, ASCII_ISUPPER(fmt_spec) ? "NAN" : "nan");
                  str_arg_l = 3;
                  zero_padding = 0;
               } ei (isinf(f)) {
                  STRCPY(tmp, infinity_str(f > 0.0, fmt_spec, force_sign, space_for_positive));
                  str_arg_l = STRLEN(tmp);
                  zero_padding = 0;
               } else {
                  // Regular float number
                  format[0] = '%';
                  l = 1;
                  if (force_sign)
                     format[l++] = space_for_positive ? ' ' : '+';
                  if (precision_specified) {
                     Unt max_prec = TMP_LEN - 10;

                     // Make sure we don't get more digits than we
                     // have room for.
                     if ((fmt_spec == 'f' || fmt_spec == 'F') && abs_f > 1.0)
                        max_prec -= (Unt)log10(abs_f);
                     if (precision > max_prec)
                        precision = max_prec;
                     l += SPRINTF(format + l, ".%d", (int)precision);
                  }
                  format[l] = fmt_spec == 'F' ? 'f' : fmt_spec;
                  format[l + 1] = ZERO;

                  str_arg_l = SPRINTF(tmp, format, f);
               }

               if (remove_trailing_zeroes) {
                  int i;
                  CS tp;

                  // Using %g or %G: remove superfluous zeroes.
                  if (fmt_spec == 'f' || fmt_spec == 'F')
                     tp = tmp + str_arg_l - 1;
                  else {
                     tp = firstOccurrence((CS)tmp, fmt_spec == 'e' ? 'e' : 'E');
                     if (tp) {
                        // Remove superfluous '+' and leading zeroes from the exponent.
                        if (tp[1] == '+') {
                           // Change "1.0e+07" to "1.0e07"
                           STRMOVE(tp + 1, tp + 2);
                           --str_arg_l;
                        }
                        i = (tp[1] == '-') ? 2 : 1;
                        while (tp[i] == '0') {
                           // Change "1.0e07" to "1.0e7"
                           STRMOVE(tp + i, tp + i + 1);
                           --str_arg_l;
                        }
                        --tp;
                     }
                  }

                  if (tp && !precision_specified) {
                     // Remove trailing zeroes, but keep the one just after a dot.
                     while (tp > tmp + 2 && *tp == '0' && tp[-1] != '.') {
                         STRMOVE(tp, tp + 1);
                         --tp;
                         --str_arg_l;
                     }
                  } 
               } else {
                  // Be consistent: some printf("%e") use 1.0e+12
                  // and some 1.0e+012.  Remove one zero in the last case.
                  CS tp = firstOccurrence((CS)tmp, fmt_spec == 'e' ? 'e' : 'E');
                  if (tp != NULL && (tp[1] == '+' || tp[1] == '-')
                       && tp[2] == '0'
                       && eeIsDigit(tp[3])
                       && eeIsDigit(tp[4])
                  ) {
                     STRMOVE(tp + 2, tp + 3);
                     --str_arg_l;
                  }
               }
            }
            if (zero_padding && min_field_width > str_arg_l && (tmp[0] == '-' || force_sign)) {
               // padding 0's should be inserted after the sign
               number_of_zeros_to_pad = min_field_width - str_arg_l;
               zero_padding_insertion_ind = 1;
            }
            str_arg = tmp;
            break;
         }

         default:
            // unrecognized conversion specifier, keep format string as-is
            zero_padding = 0;  // turn zero padding off for non-numeric conversion
            justify_left = 1;
            min_field_width = 0;          // reset flags

            // discard the unrecognized conversion, just keep *
            // the unrecognized conversion character
            str_arg = p;
            str_arg_l = 0;
            if (*p != ZERO)
                str_arg_l++;  // include invalid conversion specifier
                    // unchanged if not at end-of-string
            break;
         }

         if (*p != ZERO)
            p++;     // step over the just processed conversion specifier

         // insert padding to the left as requested by min_field_width;
         // this does not include the zero padding in case of numerical conversions
         if (!justify_left) {
            // left padding with blank or zero
            int pn = (int)(min_field_width - (str_arg_l + number_of_zeros_to_pad));

            if (pn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;
                  memset(
                     str + str_l, zero_padding ? '0' : ' ', (Unt)pn > avail ? avail : (Unt)pn
                  );
               }
               str_l += pn;
            }
         }

         //zero padding as requested by the precision or by the minimal
         //field width for numeric conversions required?
         if (number_of_zeros_to_pad == 0) {
            //will not copy first part of numeric right now, *
            //force it to be copied later in its entirety
            zero_padding_insertion_ind = 0;
         } else {
            // insert first part of numerics (sign or '0x') before zero padding
            int zn = (int)zero_padding_insertion_ind;

            if (zn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;
                  MEMMOVE(str + str_l, str_arg, (Unt)zn > avail ? avail : (Unt)zn);
               }
               str_l += zn;
            }

            // insert zero padding as requested by the precision or min field width
            zn = (int)number_of_zeros_to_pad;
            if (zn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;
                  memset(str + str_l, '0', (Unt)zn > avail ? avail : (Unt)zn);
               }
               str_l += zn;
            }
         }

         // insert formatted string
         // (or as-is conversion specifier for unknown conversions)
         {
         int sn = (int)(str_arg_l - zero_padding_insertion_ind);

         if (sn > 0) {
            if (str_l < str_m) {
               Unt avail = str_m - str_l;
               MEMMOVE(
                  str + str_l, str_arg + zero_padding_insertion_ind, 
                  (Unt)sn > avail ? avail : (Unt)sn
               );
            }
            str_l += sn;
         }
         }

         // insert right padding
         if (justify_left) {
            // right blank padding to the field width
            int pn = (int)(min_field_width - (str_arg_l + number_of_zeros_to_pad));

            if (pn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;

                  memset(str + str_l, ' ', (Unt)pn > avail ? avail : (Unt)pn);
               }
               str_l += pn;
            }
         }
         eeglFree(tofree);
      }
    }

   if (str_m > 0) {
      // make sure the string is ZERO-terminated even at the expense of
      // overwriting the last character (shouldn't happen, but just in case)
      //
      str[str_l <= str_m - 1 ? str_l : str_m - 1] = '\0';
   }

   if (tvs != NULL && tvs[num_posarg != 0 ? num_posarg : arg_idx - 1].tag != VAR_UNKNOWN)
      emsg(_(e_too_many_arguments_to_printf));

error:
   eeglFree((Byte*)ap_types);
   va_end(ap);

   //Return the number of characters formatted (excluding trailing ZERO
   //character), that is, the number of characters that would have been
   //written to the buffer if it were large enough.
   return str_l;
}

//Implementation of the format operator 'gq'.
void
op_format(Operator* oper, int keep_cursor){ //keep cursor on same text char
   long old_line_count = curBook->mem.lineCount;

   //Place the cursor where the "gq" or "gw" command was given, so that "u" can put it back there.
   curPor->cursor = oper->cursor_start;

   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return;
   curPor->cursor = oper->start;

   if (oper->is_VIsual)
      // When there is no change: need to remove the Visual selection
      drawCurBookLater(UPD_INVERTED);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
      // Set '[ mark at the start of the formatted area
      curBook->opStart = oper->start;

   // For "gw" remember the cursor position and put it back below (adjusted
   // for joined and split lines).
   if (keep_cursor)
      saved_cursor = oper->cursor_start;

   format_lines(oper->line_count, keep_cursor);

   // Leave the cursor at the first non-blank of the last formatted line.
   // If the cursor was moved one line back (e.g. with "Q}") go to the next
   // line, so "." will do the next lines.
   if (oper->end_adjusted && curPor->cursor.lnum < curBook->mem.lineCount)
      ++curPor->cursor.lnum;
   beginline(BL_WHITE | BL_FIX);
   old_line_count = curBook->mem.lineCount - old_line_count;
   msgmore(old_line_count);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
      // put '] mark on the end of the formatted area
      curBook->opEnd = curPor->cursor;

   if (keep_cursor) {
      curPor->cursor = saved_cursor;
      saved_cursor.lnum = 0;

      // formatting may have made the cursor position invalid
      check_cursor();
   }

   if (oper->is_VIsual) {
      Portal* po;
      FOR_ALL_PORTALS(po) {
         if (po->prevVisualEnd != 0) {
            // When lines have been inserted or deleted, adjust the end of
            // the Visual area to be redrawn.
            if (po->prevVisualEnd > po->oldVisualLnum)
               po->prevVisualEnd += old_line_count;
            else
               po->oldVisualLnum += old_line_count;
         }
      }
   }
}

// Implementation of the format operator 'gq' for when using 'formatexpr'.
void
op_formatexpr(Operator* oper) {
   if (oper->is_VIsual)
      // When there is no change: need to remove the Visual selection
      drawCurBookLater(UPD_INVERTED);

   if (fex_format(oper->start.lnum, oper->line_count, ZERO) != 0)
      // As documented: when 'formatexpr' returns non-zero fall back to internal formatting.
      op_format(oper, false);
}

int
fex_format(LineNr lnum, long count, int c) {  // character to be inserted
   ScriptPos   save_sctx = scriptPosG;

   // Set v:lnum to the first line number and v:count to the number of lines.
   // Set v:char to the character to be inserted (can be ZERO).
   set_EeglVar_nr(VV_LNUM, lnum);
   set_EeglVar_nr(VV_COUNT, count);
   set_EeglVar_char(c);

   // Make a copy, the option could be changed while calling it.
   CS fex = copyStr(curBook->o.formatExpr);
   scriptPosG = curBook->o.scriptLocs[PORT_foldExpr];

   // Evaluate the function.
   int r = (int)eval_to_number(fex, true);

   set_EeglVar_string(VV_CHAR, NULL, -1);
   eeglFree(fex);
   scriptPosG = save_sctx;

   return r;
}

//Escape "string" for use as a shell argument with system().
//This uses single quotes.
//Escape a newline, depending on the 'shell' option. When "do_special" is true also replace 
//"!", "%", "#" and things starting
//with "<" like "<cfile>".
//When "do_newline" is false do not escape newline unless it is csh shell.
//Return the result in allocated memory, NULL if we have run out.
CS
copyStr_shellescape(CS string, int do_special, int do_newline) {
   Unt l;

   // First count the number of extra bytes required.
   Unt length = STRLEN(string) + 3;  // two quotes and a trailing ZERO
   CS p; 
   for (p = string; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == '\'') {
         length += 3;      // ' => '\''
      }
      if ((*p == '\n' && do_newline) || (*p == '!' && do_special)) {
         ++length;         // insert backslash
      }
      if (do_special && find_commline_var(p, &l) >= 0) {
         ++length;         // insert backslash
         p += l - 1;
      }
   }

   // Allocate memory for the result and fill it.
   CS escaped_string = alloc(length);
   CS d = escaped_string;

   // add opening quote
   *d++ = '\'';

   for (p = string; *p != ZERO; ) {
      if (*p == '\'') {
         *d++ = '\'';
         *d++ = '\\';
         *d++ = '\'';
         *d++ = '\'';
         ++p;
         continue;
      }
      if ((*p == '\n' && do_newline) || (*p == '!' && do_special)) {
         *d++ = '\\';
         *d++ = *p++;
         continue;
      }
      if (do_special && find_commline_var(p, &l) >= 0) {
         *d++ = '\\';      // insert backslash
         memcpy(d, p, l);   // copy the var
         d += l;
         p += l;
         continue;
      }

      MB_COPY_CHAR(p, d);
   }

   // add terminating quote and finish with a ZERO
      *d++ = '\'';
   *d = ZERO;

    return escaped_string;
}


//}}}
//{{{json

// json.c: Encoding and decoding JSON.
// Follows this standard: https://tools.ietf.org/html/rfc7159.html
#define USING_FLOAT_STUFF

private int json_encode_item(ArrayList *gap, Var *val, int copyID, int options);

// Encode "val" into a JSON format string. The result is added to "gap"
// Returns FAIL on failure and makes gap->c empty.
private int
json_encode_gap(ArrayList* gap, Var* val, int options) {
   if (json_encode_item(gap, val, get_copyID(), options) == FAIL) {
      ga_clear(gap);
      gap->c = copyStr(E);
      return FAIL;
   }
   return OK;
}

//Encode "val" into a JSON format string. The result is in allocated memory.
//The result is empty when encoding fails. "options" can contain JSON_NO_NONE and JSON_NL.
CS
json_encode(Var* val, int options) {
   ArrayList ga;

   // Store bytes in the growarray.
   ga_init2(&ga, 1, 4000);
   json_encode_gap(&ga, val, options);
   ga_append(&ga, ZERO);
   return ga.c;
}

//Encode ["nr", "val"] into a JSON format string in allocated memory.
//"options" can contain JSON_NO_NONE and JSON_NL.
//Return NULL when out of memory.
CS
json_encode_nr_expr(int nr, Var* val, int options) {
   Var   listtv;
   Var   nrtv;
   ArrayList   ga;

   nrtv.tag = VAR_NUMBER;
   nrtv.number = nr;
   allocReturnList(&listtv);
   if (list_append_tv(listtv.list, &nrtv) == FAIL || list_append_tv(listtv.list, val) == FAIL) {
      list_unref(listtv.list);
      return NULL;
   }

   ga_init2(&ga, 1, 4000);
   if (json_encode_gap(&ga, &listtv, options) == OK && (options & JSON_NL))
      ga_append(&ga, '\n');
   list_unref(listtv.list);
   ga_append(&ga, ZERO);
   return ga.c;
}

//Encode "val" into a JSON format string prefixed by the LSP HTTP header. NULL when out of memory.
CS
json_encode_lsp_msg(Var* val) {
   ArrayList   ga;

   ga_init2(&ga, 1, 4000);
   if (json_encode_gap(&ga, val, 0) == FAIL)
      return NULL;
   ga_append(&ga, ZERO);

   ArrayList lspga;
   ga_init2(&lspga, 1, 4000);
   // Header according to LSP specification.
   eeSnprintf(IObuff, IOSIZE, (CS)"Content-Length: %u\r\n\r\n", ga.len - 1);
   ga_concat(&lspga, IObuff);
   ga_concat_len(&lspga, ga.c, ga.len);
   ga_clear(&ga);
   return lspga.c;
}

//Lookup table to quickly know if the given ASCII character must be escaped.
private const char ascii_needs_escape[128] = {
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x0.
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x1.
   0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x2.
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x3.
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x4.
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, // 0x5.
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x6.
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x7.
};

//Encode the utf-8 encoded string "str" into "gap".
private void
write_string(ArrayList* gap, CS str) {
   CS res = str;
   Unt c;

   if (!res) {
      ga_concat(gap, (CS)"\"\"");
      return;
   }

   ga_append(gap, '"');
   // `from` is the beginning of a sequence of bytes we can directly copy from
   // the input string, avoiding the overhead associated to decoding/encoding them.
   CS from = res;
   Byte numbuf[NUMBUFLEN];
   while ((c = *res) != ZERO) {
      // always use utf-8 encoding, ignore 'encoding'
      if (c < 0x80) {
         if (!ascii_needs_escape[c]) {
            res += 1;
            continue;
         }

         if (res != from)
            ga_concat_len(gap, from, res - from);
         from = res + 1;

          switch (c) {
         case 0x08:
             ga_append(gap, '\\'); ga_append(gap, 'b'); break;
         case 0x09:
             ga_append(gap, '\\'); ga_append(gap, 't'); break;
         case 0x0a:
             ga_append(gap, '\\'); ga_append(gap, 'n'); break;
         case 0x0c:
             ga_append(gap, '\\'); ga_append(gap, 'f'); break;
         case 0x0d:
             ga_append(gap, '\\'); ga_append(gap, 'r'); break;
         case 0x22: // "
         case 0x5c: // backslash
             ga_append(gap, '\\');
             ga_append(gap, c);
             break;
         default:
             eeSnprintf(numbuf, NUMBUFLEN, (CS)"\\u%04lx", (long)c);
             ga_concat(gap, numbuf);
          }

          res += 1;
      } else {
         int l = utf_ptr2len(res);

         if (l > 1) {
            res += l;
            continue;
         }

         // Invalid utf-8 sequence, replace it with the Unicode replacement character U+FFFD.
         if (res != from)
            ga_concat_len(gap, from, res - from);
         from = res + 1;

         numbuf[mb_char2bytes(0xFFFD, numbuf)] = ZERO;
         ga_concat(gap, numbuf);

         res += l;
      }
   }

   if (res != from)
      ga_concat_len(gap, from, res - from);

   ga_append(gap, '"');
}

//Encode "val" into "gap". Return FAIL or OK.
private int
json_encode_item(ArrayList *gap, Var *val, int copyID, int options) {
   Byte numbuf[NUMBUFLEN];
   CS res;
   Blob* b;
   List* l;
   Bag* d;
   int i;

   switch (val->tag) {
   case VAR_BOOL:
      switch ((long)val->number) {
         case VVAL_FALSE: ga_concat(gap, S"false"); break;
         case VVAL_TRUE: ga_concat(gap, S"true"); break;
      }
      break;

   case VAR_SPECIAL:
      switch ((long)val->number) {
      case VVAL_NONE: 
      case VVAL_NULL: ga_concat(gap, (CS)"null"); break;
      }
      break;

   case VAR_NUMBER:
      eeSnprintf(numbuf, NUMBUFLEN, (CS)"%ld", (Long)val->number);
      ga_concat(gap, numbuf);
      break;

   case VAR_STRING:
      res = val->string;
      write_string(gap, res);
      break;

   case VAR_FUNC:
   case VAR_PARTIAL:
   case VAR_JOB:
   case VAR_CHANNEL:
      showErrFmtMsg(_(e_cannot_json_encode_str), vartype_name(val->tag));
      return FAIL;

   case VAR_BLOB:
      b = val->blob;
      if (b == NULL || b->c.len == 0)
         ga_concat(gap, S"[]");
      else {
         ga_append(gap, '[');
         for (i = 0; i < b->c.len; i++) {
            if (i > 0)
               ga_concat(gap, S",");
            eeSnprintf(numbuf, NUMBUFLEN, "%d", blob_get(b, i));
            ga_concat(gap, numbuf);
         }
         ga_append(gap, ']');
      }
      break;

   case VAR_LIST:
      l = val->list;
      if (!l)
         ga_concat(gap, S"[]");
      else {
         if (l->copyId == copyID)
             ga_concat(gap, S"[]");
         else {
            ListItem   *li;

            l->copyId = copyID;
            ga_append(gap, '[');
            CHECK_LIST_MATERIALIZE(l);
            for (li = l->first; li != NULL && !gotInterruptG; ) {
               if (json_encode_item(gap, &li->c, copyID, 0) == FAIL)
                  return FAIL;
               li = li->next;
               if (li)
                  ga_append(gap, ',');
            }
            ga_append(gap, ']');
            l->copyId = 0;
         }
      }
      break;

   case VAR_BAG:
      d = val->bag;
      if (!d)
         ga_concat(gap, (CS)"{}");
      else {
         if (d->copyId == copyID)
            ga_concat(gap, (CS)"{}");
         else {
            int      first = true;
            int      todo = (int)d->hashTable.count;
            EeSetItem   *hi;

            d->copyId = copyID;
            ga_append(gap, '{');

            for (hi = d->hashTable.array; todo > 0 && !gotInterruptG; ++hi) {
               if (!HASHITEM_EMPTY(hi)) {
                   --todo;
                   if (first)
                  first = false;
                   else
                  ga_append(gap, ',');
                  write_string(gap, hi->hi_key);
                  ga_append(gap, ':');
                  if (json_encode_item(gap, &bagLookup(hi)->c, copyID, options | JSON_NO_NONE) 
                        == FAIL
                  )
                     return FAIL;
               }
            } 
            ga_append(gap, '}');
            d->copyId = 0;
         }
       }
       break;

   case VAR_FLOAT:
      if (isnan(val->floatt))
         ga_concat(gap, (CS)"NaN");
      ei (isinf(val->floatt)) {
         if (val->floatt < 0.0)
            ga_concat(gap, (CS)"-Infinity");
         else
            ga_concat(gap, (CS)"Infinity");
      } else {
         eeSnprintf(numbuf, NUMBUFLEN, "%g", val->floatt);
         ga_concat(gap, numbuf);
      }
      break;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
       internal_error_no_abort((CS)"json_encode_item()");
       return FAIL;
    }
    return OK;
}

// When "reader" has less than NUMBUFLEN bytes available, call the fill callback to get more.
private void
fill_numbuflen(JsReader* reader) {
   if (reader->js_fill && (int)(reader->js_end - reader->js_buf) - reader->js_used < NUMBUFLEN
         && reader->js_fill(reader)
   )
      reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
}

// Skip white space in "reader".  All characters <= space are considered whitespace.
// Also tops up readahead when needed.
private void
json_skip_white(JsReader* reader) {
   for (;;) {
      Unt c = reader->js_buf[reader->js_used];
      if (reader->js_fill != NULL && c == ZERO) {
         if (reader->js_fill(reader)) {
            reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
            continue;
         }
      }
      if (c == ZERO || c > ' ')
         break;
      ++reader->js_used;
   }
   fill_numbuflen(reader);
}

private int
json_decode_string(JsReader* reader, Var* res, int quote) {
   ArrayList    ga;
   int len;
   Unt c;
   Long   nr;

   if (res)
      ga_init2(&ga, 1, 200);

   CS p = reader->js_buf + reader->js_used + 1; // skip over " or '
   while (*p != quote) {
      // The JSON is always expected to be utf-8, thus use utf functions
      // here. The string is converted below if needed.
      if (*p == ZERO || p[1] == ZERO || utf_ptr2len(p) < utf_byte2len(*p)) {
         // Not enough bytes to make a character or end of the string. Get
         // more if possible.
         if (reader->js_fill == NULL)
            break;
         len = (int)(reader->js_end - p);
         reader->js_used = (int)(p - reader->js_buf);
         if (!reader->js_fill(reader))
            break; // didn't get more
         p = reader->js_buf + reader->js_used;
         reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
         continue;
      }

      if (*p == '\\') {
         c = -1;
         switch (p[1]) {
         case '\\': c = '\\'; break;
         case '"': c = '"'; break;
         case 'b': c = BS; break;
         case 't': c = TAB; break;
         case 'n': c = NL; break;
         case 'f': c = FF; break;
         case 'r': c = ENTER; break;
         case 'u':
            if (reader->js_fill != NULL && (int)(reader->js_end - p) < NUMBUFLEN) {
               reader->js_used = (int)(p - reader->js_buf);
               if (reader->js_fill(reader)) {
                  p = reader->js_buf + reader->js_used;
                  reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
               }
            }
            nr = 0;
            len = 0;
            readLongNumber(p + 2, NULL, &len, STR2NR_HEX + STR2NR_FORCE, &nr, NULL, 4, true, NULL);
            if (len == 0) {
               if (res)
                  ga_clear(&ga);
               return FAIL;
            }
            p += len + 2;
            if (0xd800 <= nr && nr <= 0xdfff
                && (int)(reader->js_end - p) >= 6
                && *p == '\\' && *(p+1) == 'u'
            ) {
               Long   nr2 = 0;

               // decode surrogate pair: \ud812\u3456
               len = 0;
               readLongNumber(p + 2, NULL, &len, STR2NR_HEX + STR2NR_FORCE, &nr2, NULL, 4, true, NULL);
               if (len == 0) {
                  if (res != NULL)
                     ga_clear(&ga);
                  return FAIL;
               }
               if (0xdc00 <= nr2 && nr2 <= 0xdfff) {
                   p += len + 2;
                   nr = (((nr - 0xd800) << 10) |
                  ((nr2 - 0xdc00) & 0x3ff)) + 0x10000;
               }
            }
            if (res) {
               Byte buf[NUMBUFLEN];

               buf[mb_char2bytes((int)nr, buf)] = ZERO;
               ga_concat(&ga, buf);
            }
            break;
         default:
            // not a special char, skip over backslash
            ++p;
            continue;
         }
         if (c > 0) {
            p += 2;
            if (res)
               ga_append(&ga, c);
         }
      } else {
         len = utf_ptr2len(p);
         if (res) {
            if (ga_grow(&ga, len) == FAIL) {
               ga_clear(&ga);
               return FAIL;
            }
            MEMMOVE((Byte *)ga.c + ga.len, p, (Unt)len);
            ga.len += len;
         }
         p += len;
      }
   }

   reader->js_used = (int)(p - reader->js_buf);
   if (*p == quote) {
      ++reader->js_used;
      if (res != NULL) {
         ga_append(&ga, ZERO);
         res->tag = VAR_STRING;
         res->string = ga.c;
      }
      return OK;
   }
   if (res != NULL) {
      res->tag = VAR_SPECIAL;
      res->number = VVAL_NONE;
      ga_clear(&ga);
   }
   return MAYBE;
}

typedef enum {
   JSON_ARRAY,      // parsing items in an array
   JSON_OBJECT_KEY,   // parsing key of an object
   JSON_OBJECT      // parsing item in an object, after the key
} JsonDecodeType;

typedef struct {
   JsonDecodeType jd_type;
   Var jd_tv;   // the list or dict
   Var jd_key_tv;
   CS key;
} JsonDecodeItem;

// Decode one item and put it in "res".  If "res" is NULL only advance. Must already have skipped 
// white space. Return FAIL for a decoding error (and give an error). Return MAYBE for an 
// incomplete message.
private int
json_decode_item(JsReader* reader, Var *res) {
   int i;
   int len;
   int retval;
   ArrayList stack;
   JsonDecodeItem* topJson;
   Byte key_buf[NUMBUFLEN];

   ga_init2(&stack, sizeof(JsonDecodeItem), 100);
   Var* cur_item = res;
   Var item;
   initVarToNull(OUT &item);
   if (res)
      initVarToNull(OUT res);

   fill_numbuflen(reader);
   CS p = reader->js_buf + reader->js_used;
   for (;;) {
      topJson = NULL;
      if (stack.len > 0) {
         topJson = ((JsonDecodeItem *)stack.c) + stack.len - 1;
         json_skip_white(reader);
         p = reader->js_buf + reader->js_used;
         if (*p == ZERO) {
            retval = MAYBE;
            goto theend;
         }
         if (topJson->jd_type == JSON_OBJECT_KEY || topJson->jd_type == JSON_ARRAY) {
            // Check for end of object or array.
            if (*p == (topJson->jd_type == JSON_ARRAY ? ']' : '}')) {
               ++reader->js_used; // consume the ']' or '}'
               --stack.len;
               if (stack.len == 0) {
                  retval = OK;
                  goto theend;
               }
               if (cur_item != NULL)
                  cur_item = &topJson->jd_tv;
               goto item_end;
            }
         }
      }

      switch (*p) {
      case '[': // start of array
         if (topJson && topJson->jd_type == JSON_OBJECT_KEY) {
            retval = FAIL;
            break;
         }
         if (ga_grow(&stack, 1) == FAIL) {
            retval = FAIL;
            break;
         }
         if (cur_item) {
            allocReturnList(cur_item);
         }

         ++reader->js_used; // consume the '['
         topJson = ((JsonDecodeItem *)stack.c) + stack.len;
         topJson->jd_type = JSON_ARRAY;
         ++stack.len;
         if (cur_item != NULL) {
            topJson->jd_tv = *cur_item;
            cur_item = &item;
         }
         continue;

      case '{': // start of object
         if (topJson && topJson->jd_type == JSON_OBJECT_KEY) {
            retval = FAIL;
            break;
         }
         if (ga_grow(&stack, 1) == FAIL) {
            retval = FAIL;
            break;
         }
         if (cur_item) {
            allocReturnList(cur_item);
         } 

         ++reader->js_used; // consume the '{'
         topJson = ((JsonDecodeItem *)stack.c) + stack.len;
         topJson->jd_type = JSON_OBJECT_KEY;
         ++stack.len;
         if (cur_item) {
            topJson->jd_tv = *cur_item;
            cur_item = &topJson->jd_key_tv;
         }
         continue;

      case '"': // string
         retval = json_decode_string(reader, cur_item, *p);
         break;

      case '\'':
         showErrFmtMsg(_(e_json_decode_error_at_str), p);
         retval = FAIL;
         break;

      case ',': // comma: empty item
         showErrFmtMsg(_(e_json_decode_error_at_str), p);
         retval = FAIL;
         break;
         // FALLTHROUGH
      case ZERO: // empty
         if (cur_item != NULL) {
            cur_item->tag = VAR_SPECIAL;
            cur_item->number = VVAL_NONE;
         }
         retval = OK;
         break;

      default:
         if (EE_ISDIGIT(*p) || (*p == '-' && (EE_ISDIGIT(p[1]) || p[1] == ZERO))) {
            CS sp = p;

            if (*sp == '-') {
               ++sp;
               if (*sp == ZERO) {
                  retval = MAYBE;
                  break;
               }
               if (!EE_ISDIGIT(*sp)) {
                  showErrFmtMsg(_(e_json_decode_error_at_str), p);
                  retval = FAIL;
                  break;
               }
            }
            sp = skipdigits(sp);
            if (*sp == '.' || *sp == 'e' || *sp == 'E') {
               if (cur_item == NULL) {
                  double f;
                  len = string2float(p, OUT &f, false);
               } else {
                  cur_item->tag = VAR_FLOAT;
                  len = string2float(p, OUT &cur_item->floatt, false);
               }
            } else {
               Long nr;

               readLongNumber(reader->js_buf + reader->js_used,
                   NULL, &len, 0, // what
                   &nr, NULL, 0, true, NULL);
               if (len == 0) {
                  showErrFmtMsg(_(e_json_decode_error_at_str), p);
                  retval = FAIL;
                  goto theend;
               }
               if (cur_item != NULL) {
                  cur_item->tag = VAR_NUMBER;
                  cur_item->number = nr;
               }
            }
            reader->js_used += len;
            retval = OK;
            break;
         }
         if (STRNICMP(p, "false", 5) == 0) {
            reader->js_used += 5;
            if (cur_item != NULL) {
                cur_item->tag = VAR_BOOL;
                cur_item->number = VVAL_FALSE;
            }
            retval = OK;
            break;
         }
         if (STRNICMP(p, "true", 4) == 0) {
            reader->js_used += 4;
            if (cur_item != NULL) {
               cur_item->tag = VAR_BOOL;
               cur_item->number = VVAL_TRUE;
            }
            retval = OK;
            break;
         }
         if (STRNICMP(p, "null", 4) == 0) {
            reader->js_used += 4;
            if (cur_item != NULL) {
               cur_item->tag = VAR_SPECIAL;
               cur_item->number = VVAL_NULL;
            }
            retval = OK;
            break;
         }
         if (STRNICMP(p, "NaN", 3) == 0) {
            reader->js_used += 3;
            if (cur_item != NULL) {
                cur_item->tag = VAR_FLOAT;
                cur_item->floatt = NAN;
            }
            retval = OK;
            break;
         }
         if (STRNICMP(p, "-Infinity", 9) == 0) {
            reader->js_used += 9;
            if (cur_item != NULL) {
               cur_item->tag = VAR_FLOAT;
               cur_item->floatt = -INFINITY;
            }
            retval = OK;
            break;
         }
         if (STRNICMP(p, "Infinity", 8) == 0) {
            reader->js_used += 8;
            if (cur_item != NULL) {
               cur_item->tag = VAR_FLOAT;
               cur_item->floatt = INFINITY;
            }
            retval = OK;
            break;
         }
         // check for truncated name
         len = (int)(reader->js_end - (reader->js_buf + reader->js_used));
         if (
             (len < 5 && STRNICMP(p, "false", len) == 0)
             || (len < 9 && STRNICMP(p, "-Infinity", len) == 0)
             || (len < 8 && STRNICMP(p, "Infinity", len) == 0)
             || (len < 3 && STRNICMP(p, "NaN", len) == 0)
             || (len < 4 && (STRNICMP(p, "true", len) == 0 || STRNICMP(p, "null", len) == 0))
         ) {
            retval = MAYBE;
         } else
            retval = FAIL;
         break;
      }

      // We are finished when retval is FAIL or MAYBE and when at the toplevel.
      if (retval == FAIL)
         break;
      if (retval == MAYBE || stack.len == 0)
         goto theend;

      if (topJson && topJson->jd_type == JSON_OBJECT_KEY && cur_item != NULL) {
         if (cur_item->tag == VAR_FLOAT) {
            // cannot use a float as a key
            emsg(_(e_using_float_as_string));
            retval = FAIL;
            goto theend;
         }
         topJson->key = convertVarToString(cur_item, key_buf);
         if (topJson->key == NULL) {
            emsg(_(e_invalid_argument));
            retval = FAIL;
            goto theend;
         }
      }

   item_end:
      topJson = ((JsonDecodeItem *)stack.c) + stack.len - 1;
      switch (topJson->jd_type) {
      case JSON_ARRAY:
         if (res) {
            ListItem   *li = listitem_alloc();
            li->c = *cur_item;
            list_append(topJson->jd_tv.list, li);
         }
         if (cur_item)
            cur_item = &item;

         json_skip_white(reader);
         p = reader->js_buf + reader->js_used;
         if (*p == ',')
             ++reader->js_used;
         ei (*p != ']') {
             if (*p == ZERO)
            retval = MAYBE;
             else {
            showErrFmtMsg(_(e_json_decode_error_at_str), p);
            retval = FAIL;
             }
             goto theend;
         }
         break;

      case JSON_OBJECT_KEY:
         json_skip_white(reader);
         p = reader->js_buf + reader->js_used;
         if (*p != ':') {
            if (cur_item != NULL)
               clearVar(cur_item);
            if (*p == ZERO)
               retval = MAYBE;
            else {
               showErrFmtMsg(_(e_json_decode_error_at_str), p);
               retval = FAIL;
            }
            goto theend;
         }
         ++reader->js_used;
         json_skip_white(reader);
         topJson->jd_type = JSON_OBJECT;
         if (cur_item != NULL)
            cur_item = &item;
         break;

      case JSON_OBJECT:
         if (cur_item != NULL && bagHasKey(topJson->jd_tv.bag, mbText(topJson->key))){
            showErrFmtMsg(_(e_duplicate_key_in_json_str), topJson->key);
            clearVar(cur_item);
            retval = FAIL;
            goto theend;
         }

         if (cur_item) {
            DictItem *di = dictitem_alloc(mbText(topJson->key));

            clearVar(&topJson->jd_key_tv);
            di->c = *cur_item;
            di->c.lock = 0;
            if (bagAdd(topJson->jd_tv.bag, di) == FAIL) {
               dictitem_free(di);
               retval = FAIL;
               goto theend;
            }
         }

         json_skip_white(reader);
         p = reader->js_buf + reader->js_used;
         if (*p == ',')
            ++reader->js_used;
         ei (*p != '}') {
            if (*p == ZERO)
               retval = MAYBE;
            else {
               showErrFmtMsg(_(e_json_decode_error_at_str), p);
               retval = FAIL;
            }
            goto theend;
         }
         topJson->jd_type = JSON_OBJECT_KEY;
         if (cur_item)
             cur_item = &topJson->jd_key_tv;
         break;
      }
   }

   // Get here when parsing failed.
   if (res != NULL) {
      clearVar(res);
      res->tag = VAR_SPECIAL;
      res->number = VVAL_NONE;
   }
   showErrFmtMsg(_(e_json_decode_error_at_str), p);

theend:
   for (i = 0; i < stack.len; i++)
      clearVar(&(((JsonDecodeItem *)stack.c) + i)->jd_key_tv);
   ga_clear(&stack);

   return retval;
}

// Decode the JSON from "reader" and store the result in "res".
// Return FAIL if not the whole message was consumed.
private int
json_decode_all(OUT Var* res, JsReader* reader) {
   // We find the end once, to avoid calling strlen() many times.
   reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
   json_skip_white(reader);
   int ret = json_decode_item(reader, res);
   if (ret != OK) {
      if (ret == MAYBE)
         showErrFmtMsg(_(e_json_decode_error_at_str), reader->js_buf);
      return FAIL;
   }
   json_skip_white(reader);
   if (reader->js_buf[reader->js_used] != ZERO) {
      showErrFmtMsg(_(e_trailing_characters_str), reader->js_buf + reader->js_used);
      return FAIL;
   }
   return OK;
}

// Decode the JSON from "reader" and store the result in "res".
// Return FAIL for a decoding error. Return MAYBE for an incomplete message. Consume the message 
// anyway.
int
json_decode(OUT Var* res, JsReader* reader) {
   // We find the end once, to avoid calling strlen() many times.
   reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
   json_skip_white(reader);
   int ret = json_decode_item(reader, res);
   json_skip_white(reader);

   return ret;
}

// Decode the JSON from "reader" to find the end of the message. "options" can be JSON_JS or zero.
// This is only used for testing. Return FAIL if the message has a decoding error.
// Return MAYBE if the message is truncated, need to read more. This only works reliable if the 
// message contains an object, array or string. A number might be truncated without knowing. Does 
// not advance the reader.
int
json_find_end(JsReader* reader) {
   int used_save = reader->js_used;
   int ret;

   // We find the end once, to avoid calling strlen() many times.
   reader->js_end = reader->js_buf + STRLEN(reader->js_buf);
   json_skip_white(reader);
   ret = json_decode_item(reader, NULL);
   reader->js_used = used_save;
   return ret;
}

void
f_json_decode(Arr(Var) argvars, Var* returnVar) {
   JsReader reader;
   reader.js_buf = tv_get_string(argvars);
   reader.js_fill = NULL;
   reader.js_used = 0;
   json_decode_all(OUT returnVar, &reader);
}

void
f_json_encode(Arr(Var) argvars, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = json_encode(argvars, 0);
}
//}}}
