//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## data.c: core data structures

#include "eegl.h"

//{{{forward declarations

private int check_for_string_or_list_or_blob_arg(Var *args, int idx);
private void dict_free(Bag *d);

//}}}
//{{{arrayList

// Clear an allocated growing array.
void
ga_clear(ArrayList* gap) {
    eeglFree(gap->c);
    ga_init(gap);
}

// Clear a growing array that contains a list of strings.
void
ga_clear_strings(ArrayList* gap) {
   int      i;

   if (gap->c != NULL) {
      for (i = 0; i < gap->len; ++i)
         eeglFree(((Byte **)(gap->c))[i]);
   } 
   ga_clear(gap);
}

// Copy a growing array that contains a list of strings.
int
ga_copy_strings(ArrayList *from, ArrayList *to) {
   int      i;
   ga_init2(to, sizeof(CS), 1);
   if (ga_grow(to, from->len) == FAIL)
      return FAIL;

   for (i = 0; i < from->len; ++i) {
      CS orig = ((Byte **)from->c)[i];
      CS copy = orig ? copyStr(orig) : null;
      ((Byte **)to->c)[i] = copy;
   }
   to->len = from->len;
   return OK;
}

// Initialize a growing array. Don't forget to set ga_itemsize and ga_growsize! Or use ga_init2()
void
ga_init(ArrayList* gap) {
    gap->c = NULL;
    gap->cap = 0;
    gap->len = 0;
}

void
ga_init2(ArrayList *gap, Unt itemsize, int growsize) {
    ga_init(gap);
    gap->ga_itemsize = (int)itemsize;
    gap->ga_growsize = growsize;
}

// Make room in growing array "gap" for at least "n" items. FAIL for failure, OK otherwise.
int
ga_grow(ArrayList *gap, int n) {
   if (gap->cap - gap->len < n)
      return ga_grow_inner(gap, n);
   return OK;
}

// Same as ga_grow() but uses an allocation id for testing.
int
ga_grow_id(ArrayList *gap, int n, AllocId id UNUSED) {
   if (alloc_fail_id == id && alloc_does_fail(sizeof(List)))
      return FAIL;

   return ga_grow(gap, n);
}

int
ga_grow_inner(ArrayList* gap, int n) {
   Unt   old_len;
   Unt   new_len;
   Byte   *pp;

   if (n < gap->ga_growsize)
      n = gap->ga_growsize;

   // A linear growth is very inefficient when the array grows big.  This
   // is a compromise between allocating memory that won't be used and too
   // many copy operations. A factor of 1.5 seems reasonable.
   if (n < gap->len / 2)
      n = gap->len / 2;

   new_len = (Unt)gap->ga_itemsize * (gap->len + n);
   pp = eeRealloc(gap->c, new_len);
   old_len = (Unt)gap->ga_itemsize * gap->cap;
   memset(pp + old_len, 0, new_len - old_len);
   gap->cap = gap->len + n;
   gap->c = pp;
   return OK;
}

//For an ArrayList that contains a list of strings: concatenate all the
//strings with a separating "sep".
//Return NULL when out of memory.
CS
ga_concat_strings(ArrayList *gap, char *sep) {
   int i;
   int len = 0;
   int sep_len = (int)STRLEN(sep);

   for (i = 0; i < gap->len; ++i)
      len += (int)STRLEN(((Byte **)(gap->c))[i]) + sep_len;

   CS s = alloc(len + 1);

   *s = ZERO;
   CS p = s;
   for (i = 0; i < gap->len; ++i) {
      if (p != s) {
          STRCPY(p, sep);
          p += sep_len;
      }
      STRCPY(p, ((Byte **)(gap->c))[i]);
      p += STRLEN(p);
   }
   return s;
}

// Make a copy of string "p" and add it to "gap". When out of memory, 
// nothing changes and FAIL is returned.
int
ga_copy_string(ArrayList *gap, CS p) {
   CS cp = copyStr(p);

   if (ga_grow(gap, 1) == FAIL) {
      eeglFree(cp);
      return FAIL;
   }
   ((Byte **)(gap->c))[gap->len] = cp;
   gap->len++;
   return OK;
}

// Add string "p" to "gap". When out of memory, FAIL is returned (caller may want to free "p").
int
ga_add_string(ArrayList *gap, CS p) {
   if (ga_grow(gap, 1) == FAIL)
      return FAIL;
   ((Byte **)(gap->c))[gap->len] = p;
   gap->len++;
   return OK;
}

// Concatenate a string to a growarray which contains bytes.
// When "s" is NULL memory allocation fails does not do anything.
// Note: Does NOT copy the ZERO at the end!
void
ga_concat(ArrayList *gap, CS s) {
   if (s == NULL || *s == ZERO)
      return;
   int len = (int)STRLEN(s);
   if (ga_grow(gap, len) == OK) {
      mch_memmove((char *)gap->c + gap->len, s, (Unt)len);
      gap->len += len;
    }
}

// Concatenate 'len' bytes from string 's' to a growarray. When "s" is NULL do not do anything.
void
ga_concat_len(ArrayList *gap, CS s, Unt len) {
   if (s == NULL || *s == ZERO || len == 0)
      return;
   if (ga_grow(gap, (int)len) == OK) {
      mch_memmove((char *)gap->c + gap->len, s, len);
      gap->len += (int)len;
   }
}

// Append one byte to a growarray which contains bytes.
int
ga_append(ArrayList *gap, int c) {
   if (ga_grow(gap, 1) == FAIL)
      return FAIL;
   *((char *)gap->c + gap->len) = c;
   ++gap->len;
   return OK;
}

// Append the text in "gap" below the cursor line and clear "gap".
void
append_ga_line(ArrayList *gap) {
   // Remove trailing CR.
   if (gap->len > 0
          && !curBook->o.binary
          && ((CS)gap->c)[gap->len - 1] == ENTER)
      --gap->len;
   ga_append(gap, ZERO);
   ml_append(curPor->cursor.lnum++, gap->c, 0, FALSE);
   gap->len = 0;
}

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
list_rem_watch(List *l, ListWatch *lwrem) {
   ListWatch   *lw;
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
list_fix_watch(List *l, ListItem *item) {
   ListWatch   *lw;

   FOR_ALL_WATCHERS(l, lw) {
      if (lw->c == item)
          lw->c = item->next;
   } 
}


// Prepend the list to the list of lists for garbage collection.
private void
registerForGc(List *l) {
   if (first_list != NULL)
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
   ListItem   *li = (ListItem *)(l + 1) + idx;
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
allocReturnList_id(Var *returnVar, AllocId id) {
   if (id == alloc_fail_id && alloc_does_fail(sizeof(List)))
      return FAIL;
   allocReturnList(returnVar);
   return OK;
}


// Set a list as the return value.  Increments the reference count.
void
returnVar_list_set(OUT Var *returnVar, List *l) {
   returnVar->tag = VAR_LIST;
   returnVar->list = l;
   if (l)
      ++l->refcount;
}

// Unreference a list: decrement the reference count and free it when it becomes zero.
void
list_unref(List *l) {
   if (l && --l->refcount <= 0)
      list_free(l);
}

// Free a list, including all non-container items it points to. Ignores the reference count.
private void
list_free_contents(List *l) {
   ListItem *item;

   if (l->first != &range_list_item) {
      for (item = l->first; item != NULL; item = l->first) {
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
   List   *ll;
   int      did_free = FALSE;

   for (ll = first_list; ll != NULL; ll = ll->usedNext) {
      if ((ll->copyId & COPYID_MASK) != (copyID & COPYID_MASK) && ll->watcher == NULL) {
          // Free the List and ordinary items it contains, but don't recurse
          // into Lists and Dictionaries, they will be in the list of dicts or list of lists.
          list_free_contents(ll);
          did_free = TRUE;
      }
   } 
   return did_free;
}

private void
list_free_list(List  *l) {
   // Remove the list from the list of lists for garbage collection.
   if (l->usedPrev == NULL)
      first_list = l->usedNext;
   else
      l->usedPrev->usedNext = l->usedNext;
   if (l->usedNext != NULL)
      l->usedNext->usedPrev = l->usedPrev;

   free_type(l->ty);
   eeglFree(l);
}

void
list_free_items(int copyID) {
   List   *ll, *ll_next;

   for (ll = first_list; ll != NULL; ll = ll_next) {
      ll_next = ll->usedNext;
      if ((ll->copyId & COPYID_MASK) != (copyID & COPYID_MASK) && ll->watcher == NULL) {
         // Free the List and ordinary items it contains, but don't recurse
         // into Lists and Dictionaries, they will be in the list of dicts or list of lists.
         list_free_list(ll);
      }
   }
}

void
list_free(List *l) {
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
listitem_free(List *l, ListItem *item) {
    clearVar(&item->c);
    list_free_item(l, item);
}

// Remove a list item from a List and free it.  Also clears the value.
void
listitem_remove(List *l, ListItem *item) {
    list_remove(l, item, item);
    listitem_free(l, item);
}

// Get the number of items in a list.
long
list_len(List *l) {
   if (l == NULL)
   return 0L;
    return l->len;
}

// Return TRUE when two lists have exactly the same values.
int
list_equal(
   List   *l1,
   List   *l2,
   int      ic)   // ignore case for strings
{
   ListItem   *item1, *item2;

   if (l1 == l2)
      return TRUE;
   if (list_len(l1) != list_len(l2))
      return FALSE;
   if (list_len(l1) == 0)
      // empty and NULL list are considered equal
      return TRUE;
   if (l1 == NULL || l2 == NULL)
      return FALSE;

   CHECK_LIST_MATERIALIZE(l1);
   CHECK_LIST_MATERIALIZE(l2);

   for (item1 = l1->first, item2 = l2->first;
        item1 != NULL && item2 != NULL;
        item1 = item1->next, item2 = item2->next
   ) {
      if (!tv_equal(&item1->c, &item2->c, ic))
         return FALSE;
   } 
   return item1 == NULL && item2 == NULL;
}

// Locate item with index "n" in list "l" and return it. A negative index is counted from the end;
// -1 is the last item. Return NULL when "n" is out of range.
ListItem *
list_find(List *l, long n) {
   ListItem   *item;
   long   idx;

   if (l == NULL)
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
list_find_nr(List   *l, long   idx, OUT Boole* errorp) {  // set to TRUE when something wrong
   ListItem   *li;

   if (l && l->first == &range_list_item) {
      long       n = idx;

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

   li = list_find(l, idx);
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
list_append(List *l, ListItem *item) {
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
list_append_tv(List *l, Var* newVal) {
   ListItem* newItem = listitem_alloc();
      
   copy_tv(OUT &newItem->c, newVal);
   list_append(l, newItem);
   return OK;
}

// Append Var "tv" to the end of list "l". "tv" is moved. 
// Return FAIL when out of memory or the tag is wrong.
private int
list_append_tv_move(List *l, Var *tv) {
   ListItem   *li = listitem_alloc();
      
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
check_range_index_one(List *l, long *n1, int quiet) {
   long   orig_n1 = *n1;
   ListItem   *li = list_find_index(l, n1);

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
check_range_index_two(
   List       *l,
   long       *n1,
   ListItem  *li1,
   long       *n2,
   int       quiet)
{
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
// TRUE then replace all items after "idx1".
// "op" is the operator, normally "=" but can be "+=" and the like.
// "varname" is used for error messages. Return OK or FAIL.
int
list_assign_range(
   List       *dest,
   List       *src,
   long       idx1_arg,
   long       idx2,
   int       empty_idx2,
   Byte       *op,
   Text varname)
{
   ListItem   *src_li;
   ListItem   *dest_li;
   long   idx1 = idx1_arg;
   ListItem   *first_li = list_find_index(dest, &idx1);
   long   idx;

   // Check whether any of the list items is locked before making any changes.
   idx = idx1;
   dest_li = first_li;
   for (src_li = src->first; src_li != NULL && dest_li != NULL; ) {
      if (value_check_lock(dest_li->c.lock, varname, FALSE))
         return FAIL;
      src_li = src_li->next;
      if (src_li == NULL || (!empty_idx2 && idx2 == idx))
         break;
      dest_li = dest_li->next;
      ++idx;
   }

   // Assign the List values to the list items.
   idx = idx1;
   dest_li = first_li;
   for (src_li = src->first; src_li != NULL; ) {
      if (op != NULL && *op != '=')
         tv_op(&dest_li->c, &src_li->c, op);
      else {
         clearVar(&dest_li->c);
         copy_tv(OUT &dest_li->c, &src_li->c);
      }
      src_li = src_li->next;
      if (src_li == NULL || (!empty_idx2 && idx2 == idx))
         break;
      if (dest_li->next == NULL) {
         // Need to add an empty item.
         if (list_append_number(dest, 0) == FAIL) {
            src_li = NULL;
            break;
         }
      }
      dest_li = dest_li->next;
      ++idx;
   }
   if (src_li != NULL) {
      emsg(_(e_list_value_has_more_items_than_targets));
      return FAIL;
   }
   if (empty_idx2
          ? (dest_li != NULL && dest_li->next != NULL)
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
list_flatten(List *list, ListItem *first, long maxitems, long maxdepth) {
   ListItem   *item;
   int      done = 0;

   if (maxdepth == 0)
      return;
   CHECK_LIST_MATERIALIZE(list);
   if (first == NULL)
      item = list->first;
   else
      item = first;

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
flatten_common(Var *argvars, Var *returnVar, int make_copy) {
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
      l = list_copy(l, FALSE, TRUE, get_copyID());
      returnVar->list = l;
      if (l == NULL)
         return;
      // The type will change.
      free_type(l->ty);
      l->ty = NULL;
   } else {
      if (value_check_lock(l->lock, text(N_("flatten() argument")), TRUE))
         return;
      ++l->refcount;
   }

    list_flatten(l, NULL, l->len, maxdepth);
}

// "flatten(list[, {maxdepth}])" function
void
f_flatten(Var *argvars, Var *returnVar) {
   flatten_common(argvars, returnVar, FALSE);
}

// "flattennew(list[, {maxdepth}])" function
void
f_flattennew(Var *argvars, Var *returnVar) {
   flatten_common(argvars, returnVar, TRUE);
}

// "items(list)" function Caller must have already checked that argvars[0] is a List.
void
list2items(Var *argvars, Var *returnVar) {
   List   *l = argvars[0].list;
   ListItem   *li;
   Long   idx;

   allocReturnList(returnVar);
   if (!l)
      return;  // null list behaves like an empty list

   // TODO: would be more efficient to not materialize the argument
   CHECK_LIST_MATERIALIZE(l);
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
string2items(Var *argvars, Var *returnVar) {
   Byte   *p = argvars[0].string;
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
   List* l = l1 ? list_copy(l1, FALSE, TRUE, 0) : list_alloc();
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
   List   *list,
   int      range,
   Long   n1_arg,
   Long   n2_arg,
   int      exclusive,
   Var   *returnVar,
   int      verbose)
{
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

// Make a copy of list "orig".  Shallow if "deep" is FALSE.
// The refcount of the new list is set to 1.
// See item_copy() for "top" and "copyID". Return NULL when out of memory.
List *
list_copy(List *orig, int deep, int top, int copyID) {
   List   *copy;
   ListItem   *item;
   ListItem   *ni;

   if (!orig)
      return NULL;

   copy = list_alloc();

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
   for (item = orig->first; item != NULL && !gotInterruptG; item = item->next) {
      ni = listitem_alloc();
      if (deep) {
         if (item_copy(&item->c, &ni->c, deep, FALSE, copyID) == FAIL) {
            eeglFree(ni);
            break;
         }
      } else
         copy_tv(OUT &ni->c, &item->c);
      list_append(copy, ni);
   }
   ++copy->refcount;
   if (item != NULL) {
     list_unref(copy);
     copy = NULL;
   }

   return copy;
}

//Remove items "item" to "item2" from list "l".
//Do not free the listitem or the value!
//This used to be called list_remove, but that conflicts with a Sun header file.
void
list_remove(List *l, ListItem *item, ListItem *item2) {
   ListItem   *ip;

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
list2string(Var *tv, int copyID, int restore_copyID) {
   if (tv->list == NULL)
      return NULL;
   ArrayList   ga;
   ga_init2(&ga, sizeof(char), 80);
   ga_append(&ga, '[');
   CHECK_LIST_MATERIALIZE(tv->list);
   if (list_join(&ga, tv->list, (CS)", ", FALSE, restore_copyID, copyID) == FAIL) {
      eeglFree(ga.c);
      return NULL;
   }
   ga_append(&ga, ']');
   ga_append(&ga, ZERO);
   return (CS)ga.c;
}

typedef struct join_S {
   Byte   *s;
   Byte   *tofree;
} Join;

private int
list_join_inner(
    ArrayList   *gap,      // to store the result in
    List   *l,
    Byte   *sep,
    int      echo_style,
    int      restore_copyID,
    int      copyID,
    ArrayList   *join_gap)   // to keep each list item string
{
    int      i;
    Join   *p;
    int      len;
    int      sumlen = 0;
    int      first = TRUE;
    Byte   *tofree;
    Byte   numbuf[NUMBUFLEN];
    ListItem   *item;
    Byte   *s;

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
         first = FALSE;
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
// When "echo_style" is TRUE use String as echoed, otherwise as inside a List.
// Return FAIL or OK.
int
list_join(
    ArrayList   *gap,
    List   *l,
    Byte   *sep,
    int      echo_style,
    int      restore_copyID,
    int      copyID)
{
    ArrayList   join_ga;
    int      retval;
    Join   *p;
    int      i;

   if (l->len < 1)
      return OK; // nothing to do
   ga_init2(&join_ga, sizeof(Join), l->len);
   retval = list_join_inner(gap, l, sep, echo_style, restore_copyID, copyID, &join_ga);

   if (join_ga.c == NULL)
      return retval;

   // Dispose each item in join_ga.
   p = (Join *)join_ga.c;
   for (i = 0; i < join_ga.len; ++i) {
      eeglFree(p->tofree);
      ++p;
   }
   ga_clear(&join_ga);

   return retval;
}

void
f_join(Var *argvars, Var *returnVar) {
   ArrayList   ga;
   Byte   *sep;

   returnVar->tag = VAR_STRING;

   if (check_for_list_arg(argvars, 0) == FAIL
          || check_for_opt_string_arg(argvars, 1) == FAIL)
      return;

   if ((argvars[0].tag == VAR_LIST && argvars[0].list == NULL))
      return;

   if (argvars[1].tag == VAR_UNKNOWN)
      sep = (CS)" ";
   else
      sep = convertVarToStringSingleUse(&argvars[1]);

   if (sep != NULL) {
      ga_init2(&ga, sizeof(char), 80);
      list_join(&ga, argvars[0].list, sep, TRUE, FALSE, 0);
      ga_append(&ga, ZERO);
      returnVar->string = (CS)ga.c;
   }
    else
   returnVar->string = NULL;
}

// Allocate a variable for a List and fill it from "*arg".
// "*arg" points to the "[". Return OK or FAIL.
int
eval_list(Byte **arg, Var *returnVar, EvalCtx *evalarg, int do_error) {
   int evaluate = evalarg == NULL ? FALSE : evalarg->eval_flags & EVAL_EVALUATE;
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
write_list(FILE *fd, List *list, int binary) {
   ListItem   *li;
   int      c;
   int      ret = OK;
   Byte   *s;

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
   List  *l = &sl->list;
   int       i;

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
f_list2str(Var *argvars, Var *returnVar) {
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
listVar_remove(Var *argvars, Var *returnVar, CS arg_errmsg) {
   List   *l;
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
   SortItem  *si1, *si2;
   Var   *tv1, *tv2;
   Byte   *p1, *p2;
   Byte   *tofree1 = NULL, *tofree2 = NULL;
   int      res;
   Byte   numbuf1[NUMBUFLEN];
   Byte   numbuf2[NUMBUFLEN];

   si1 = (SortItem *)s1;
   si2 = (SortItem *)s2;
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
      p1 = Em;
   if (p2 == NULL)
      p2 = Em;
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
   SortItem  *si1, *si2;
   int      res;
   Var   returnVar;
   Var   argv[3];
   Byte   *func_name;
   PartiallyApplied   *partial = sortinfo->item_compare_partial;
   FnExe   funcexe;
   int      anyEmsgG_before = anyEmsgG;

   // shortcut after failure in previous call; compare all items equal
   if (sortinfo->item_compare_func_err)
      return 0;

   si1 = (SortItem *)s1;
   si2 = (SortItem *)s2;

   if (!partial)
      func_name = sortinfo->item_compare_func;
   else
      func_name = partial_name(partial);

   // Copy the values.  This is needed to be able to set lock to VAR_FIXED
   // in the copy without changing the original list items.
   copy_tv(OUT &argv[0], &si1->item->c);
   copy_tv(OUT &argv[1], &si2->item->c);

   returnVar.tag = VAR_UNKNOWN;      // clearVar() uses this
   CLEAR_FIELD(funcexe);
   funcexe.fe_evaluate = TRUE;
   funcexe.fe_partial = partial;
   funcexe.fe_selfdict = sortinfo->item_compare_selfdict;
   res = call_func(func_name, -1, &returnVar, 2, argv, &funcexe);
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
parse_sort_uniq_args(Var *argvars, SortInfo *info) {
   info->item_compare_ic = FALSE;
   info->item_compare_lc = FALSE;
   info->item_compare_numeric = FALSE;
   info->item_compare_numbers = FALSE;
   info->item_compare_float = FALSE;
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
            info->item_compare_ic = TRUE;
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
            info->item_compare_numeric = TRUE;
          } ei (STRCMP(info->item_compare_func, "N") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_numbers = TRUE;
          } ei (STRCMP(info->item_compare_func, "f") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_float = TRUE;
          } ei (STRCMP(info->item_compare_func, "i") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_ic = TRUE;
          } ei (STRCMP(info->item_compare_func, "l") == 0) {
            info->item_compare_func = NULL;
            info->item_compare_lc = TRUE;
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
do_sort_uniq(Var *argvars, Var *returnVar, int sort) {
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
f_sort(Var *argvars, Var *returnVar) {
   do_sort_uniq(argvars, returnVar, TRUE);
}

// "uniq({list})" function
void
f_uniq(Var *argvars, Var *returnVar) {
   do_sort_uniq(argvars, returnVar, FALSE);
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
   if (eval_expr_typval(expr, FALSE, argv, 2, newtv) == FAIL)
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
   FilterMap   filtermap,
   CS arg_errmsg,
   Var   *expr,
   Var   *returnVar
) {
   int      prev_lock;
   List   *l_ret = NULL;
   int      idx = 0;
   int      rem;
   ListItem   *li, *nli;
   Var   newtv;

   if (filtermap == FILTERMAP_MAPNEW) {
      returnVar->tag = VAR_LIST;
      returnVar->list = NULL;
   }
   if (!l || (filtermap == FILTERMAP_FILTER && value_check_lock(l->lock, mbText(arg_errmsg), true)))
      return;

   prev_lock = l->lock;

   if (filtermap == FILTERMAP_MAPNEW) {
      allocReturnList(returnVar);
      l_ret = returnVar->list;
   }
   // set_EeglVar_nr() doesn't set the type
   set_EeglVar_type(VV_KEY, VAR_NUMBER);

   if (l->lock == 0)
      l->lock = VAR_LOCKED;

   if (l->first == &range_list_item) {
      Long   val = l->lv_u.nonmat.start;
      int      len = l->len;
      int      stride = l->lv_u.nonmat.stride;

      // List from range(): loop over the numbers
      // NOTE: foreach() returns the range_list_item
      if (filtermap != FILTERMAP_MAPNEW && filtermap != FILTERMAP_FOREACH) {
         l->first = NULL;
         l->lv_u.mat.last = NULL;
         l->len = 0;
         l->lv_u.mat.cachedItem = NULL;
      }

      for (idx = 0; idx < len; ++idx) {
         Var tv = (Var){.tag = VAR_NUMBER, .lock = 0, .number = 0};

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
filter_map(Var *argvars, Var *returnVar, FilterMap filtermap) {
    CS func_name = (CS)(
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
      showErrFmtMsg(_(msg), func_name);
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
   anyEmsgG = FALSE;

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
f_filter(Var *argvars, Var *returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_FILTER);
}

void
f_map(Var *argvars, Var *returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_MAP);
}

void
f_mapnew(Var *argvars, Var *returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_MAPNEW);
}

void
f_foreach(Var *argvars, Var *returnVar) {
   filter_map(argvars, returnVar, FILTERMAP_FOREACH);
}

// "add(list, item)" function
private void
list_add(Var *argvars, Var *returnVar) {
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
f_add(Var *argvars, Var *returnVar) {
   returnVar->number = 1; // Default: Failed

   if (argvars[0].tag == VAR_LIST)
      list_add(argvars, returnVar);
   ei (argvars[0].tag == VAR_BLOB)
      blob_add(argvars, returnVar);
   else
      emsg(_(e_list_or_blob_required));
}

//Count the number of times item "needle" occurs in List "l" starting at index
//"idx". Case is ignored if "ic" is TRUE.
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
f_count(Var *argvars, Var *returnVar) {
   long   n = 0;
   int      ic = FALSE;
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
// argvars[3] and return the resulting list in "returnVar".  "is_new" is TRUE for extendnew().
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
      l1 = list_copy(l1, FALSE, TRUE, get_copyID());
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
      *returnVar = (Var){.tag = VAR_LIST, .list = l1, .lock = FALSE};
   } else
      copy_tv(OUT returnVar, &argvars[0]);
}

// "extend()" or "extendnew()" function.  "is_new" is TRUE for extendnew().
private void
extend(Var *argvars, Var *returnVar, CS arg_errmsg, int is_new) {
   CS func_name = (CS)( is_new ? "extendnew()" : "extend()");

   if (argvars[0].tag == VAR_LIST && argvars[1].tag == VAR_LIST) {
      // Check that extend() does not change the type of the list if it was declared.
      list_extend_func(argvars, arg_errmsg, is_new, returnVar);
   } ei (argvars[0].tag == VAR_BAG && argvars[1].tag == VAR_BAG) {
      // Check that extend() does not change the type of the dict if it was declared.
      bagExtend_func(argvars, arg_errmsg, is_new, returnVar);
   } else
      showErrFmtMsg(_(e_argument_of_str_must_be_list_or_dictionary), func_name);
}

// "extend(list, list [, idx])" function. "extend(dict, dict [, action])" function
void
f_extend(Var *argvars, Var *returnVar) {
   Byte      *errmsg = (CS)N_("extend() argument");
   extend(argvars, returnVar, errmsg, FALSE);
}

// "extendnew(list, list [, idx])" function. "extendnew(dict, dict [, action])" function
void
f_extendnew(Var *argvars, Var *returnVar) {
   Byte* errmsg = (CS)N_("extendnew() argument");
   extend(argvars, returnVar, errmsg, TRUE);
}

private void
list_insert_func(Var *argvars, Var *returnVar) {
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
f_insert(Var *argvars, Var *returnVar) {
   if (argvars[0].tag == VAR_BLOB)
      blob_insert_func(argvars, returnVar);
   ei (argvars[0].tag != VAR_LIST)
      showErrFmtMsg(_(e_argument_of_str_must_be_list_or_blob), "insert()");
   else
      list_insert_func(argvars, returnVar);
}

void
f_remove(Var *argvars, Var *returnVar) {
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
list_reverse(List *l, Var *returnVar) {
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
f_reverse(Var *argvars, Var *returnVar) {
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

      r = eval_expr_typval(expr, TRUE, argv, 2, returnVar);

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
f_reduce(Var *argvars, Var *returnVar) {
   Byte   *func_name;

   if (check_for_string_or_list_or_blob_arg(argvars, 0) == FAIL)
      return;

   if (argvars[1].tag == VAR_FUNC)
      func_name = argvars[1].string;
   ei (argvars[1].tag == VAR_PARTIAL)
      func_name = partial_name(argvars[1].partial);
   else
      func_name = tv_get_string(&argvars[1]);
   if (func_name == NULL || *func_name == ZERO) {
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
f_slice(Var *argvars, Var *returnVar) {
   if (check_can_index(&argvars[0], TRUE, FALSE) != OK)
      return;

   copy_tv(OUT returnVar, argvars);
   eval_index_inner(
      returnVar, TRUE, argvars + 1,
      argvars[2].tag == VAR_UNKNOWN ? NULL : argvars + 2, TRUE, NULL, 0, FALSE
   );
}

//}}}
//{{{key-value pair

// compare two Kv structs by case sensitive value
int
cmp_keyvalue_value(const void *a, const void *b) {
   return STRCMP(((Kv*)a)->value.c, ((Kv*)b)->value.c);
}

// compare two Kv structs by value with length
int
cmp_keyvalue_value_n(const void *a, const void *b) {
   Kv *kv1 = (Kv *)a;
   Kv *kv2 = (Kv *)b;

   return STRNCMP(kv1->value.c, kv2->value.c, MAX(kv1->value.len, kv2->value.len));
}

// compare two Kv structs by case insensitive value
int
cmp_keyvalue_value_i(const void *a, const void *b) {
    Kv *kv1 = (Kv *)a;
    Kv *kv2 = (Kv *)b;

    return caseInsensitiveCompare(kv1->value.c, kv2->value.c);
}

// compare two Kv structs by case insensitive ASCII value with value.length
int
cmp_keyvalue_value_ni(const void *a, const void *b) {
    Kv *kv1 = (Kv *)a;
    Kv *kv2 = (Kv *)b;
    return compareAscii((Byte *)kv1->value.c,
       (Byte *)kv2->value.c, MAX(kv1->value.len,
          kv2->value.len));
}

//}}}
//{{{DictStringInt

// Dictionary = a hash table that is filled once and then unchanged. As opposed to the more general
// term "hash table" which is a data structure with arbitrary usage patterns.
// This particular data structure is optimized in the following ways:
// - it relies on a byte array that lives longer than itself and contains all the keys separated by
// the zero char (e.g. "asdf\0bc jk\0" for the keys "asdf" and "bc jk")
// - it stores strings as simple integers (offsets into the said byte array)
// - its values are 4-byte ints
// - after construction its length doesn't change (no key insertions or removals), though the values
// themselves may be changed

private Unt
hashCode(Byte const* start) {
   Unt result = 5381;
   Byte const* p = start;
   for (int i = 0; p[i] != ZERO; i++) {
      result = ((result << 5) + result) + p[i]; // hash*33 + c
   }
   return result;
}

private Unt
hashOfText(Text s) {
   Unt result = 5381;
   Byte const* p = s.c;
   for (Unt i = 0; i < s.len; i++) {
      result = ((result << 5) + result) + p[i]; // hash*33 + c
   }
   return result;
}

private DictStringInt128*
initDict0(Arr(Byte const) text, Int size, Arena* a, OUT Arr(Unt)* temp) {
   DictStringInt128* dict = allocate(DictStringInt128, a);
   dict->c = allocateArray(size, DictStringIntItem, a),
   dict->hashes = allocateArray(size, Unt, a),
   dict->text = text;
   memset(dict->dict, 0, 128*4);
   dict->dict[128] = size;
   
   // Temporary array which we'll free at end of function (since it's at the very end of the arena)
   *temp = allocateOnArena(size*4, a);
   
   // calculate the hashes and keys
   Byte const* ch = text;
   for (Int i = 0; i < size; i++) {
      Unt hash = hashCode(ch);
      dict->hashes[i] = ch - text; // yep, initially the keys go into @hashes!
      (*temp)[i] = hash;
      dict->dict[hash >> 25]++;
      for (; *ch != ZERO; ch++) {
      }
      ch++;
   }
   dict->textLen = ch - text;
   
   // Bucket counts -> bucket start indices
   Unt sumBefore = dict->dict[0];
   dict->dict[0] = 0;
   for (Int i = 1; i < 128; i++) {
      Unt value = dict->dict[i];
      dict->dict[i] = sumBefore;
      sumBefore += value;
   }
   return dict;
}

private void
initDict1(Arena* a, int size, Arr(Unt) temp, OUT DictStringInt128* dict) {
   // After every key & value has been put, @dict now contains not starts of buckets but their ends.
   // So not [0 5 7 .. 100 101 size] but [5 7 ... 101 size size]
   // Shift all the elements right by 1 to restore
   for (Int i = 126; i > -1; i--) {
      dict->dict[i + 1] = dict->dict[i];
   }
   dict->dict[0] = 0;
   
   // Now store the hashes into their correct buckets
   for (Int i = 0; i < size; i++) {
      Unt hash = temp[i];
      Unt ind = dict->dict[hash >> 25];
      dict->hashes[ind] = hash; 
      dict->dict[hash >> 25]++;
   }
   
   // @dict needs the same shift again
   for (Int i = 126; i > -1; i--) {
      dict->dict[i + 1] = dict->dict[i];
   }
   dict->dict[0] = 0;
   
   arenaTryFree((void*)temp, size*4, a);
}

DictStringInt128* dictStringInt128New(Arr(Byte const) text, Arr(Int) values, Int size, Arena* a) {
   Arr(Unt) temp;
   DictStringInt128* dict = initDict0(text, size, a, OUT &temp);

   // Store the keys and values into their correct buckets
   for (Int i = 0; i < size; i++) {
      Unt bucket = temp[i] >> 25;
      Unt ind = dict->dict[bucket];
      dict->c[ind] = (DictStringIntItem){.key = dict->hashes[i], .value = values[i]};
      dict->dict[bucket]++;
   }
   
   initDict1(a, size, temp, OUT dict);
   return dict;
}

// Create a dictionary where values are just indices of names
DictStringInt128* dictStringInt128NewJustIndices(Arr(Byte const) text, Int size, Arena* a) {
   Arr(Unt) temp;
   DictStringInt128* dict = initDict0(text, size, a, OUT &temp);

   // Store the keys and values into their correct buckets
   for (Int i = 0; i < size; i++) {
      Unt bucket = temp[i] >> 25;
      Unt ind = dict->dict[bucket];
      dict->c[ind] = (DictStringIntItem){.key = dict->hashes[i], .value = i};
      dict->dict[bucket]++;
   }
   
   initDict1(a, size, temp, OUT dict);
   return dict;
}

#define dictGetterFn(ifFound, ifNotFound) \
   Unt needleHash = hashCode(needle);\
   Unt ind = needleHash >> 25;\
   Unt const end = haystack->dict[ind + 1];\
   for (Unt i = haystack->dict[ind]; i < end; i++) {\
      if (haystack->hashes[i] == needleHash \
            && STRCMP(haystack->text + haystack->c[i].key, needle) == 0\
      ) {\
         ifFound;\
      }\
   }\
   ifNotFound;
   
#define dictGetterFn_Text(ifFound, ifNotFound) \
   Unt needleHash = hashOfText(needle);\
   Unt ind = needleHash >> 25;\
   Unt const end = haystack->dict[ind + 1];\
   for (Unt i = haystack->dict[ind]; i < end; i++) {\
      if (haystack->hashes[i] == needleHash \
            && haystack->c[i].key + needle.len < haystack->textLen\
            && memcmp(haystack->text + haystack->c[i].key, needle.c, needle.len) == 0\
      ) {\
         ifFound;\
      }\
   }\
   ifNotFound;


Boole containsKey_DictStringInt128(Arr(Byte const) needle, DictStringInt128* restrict haystack) {
   dictGetterFn(return true, return false);
}

Int get_DictStringInt128(Arr(Byte const) needle, DictStringInt128* restrict haystack) {
   dictGetterFn(return haystack->c[i].value, return -1); // TODO throw exception
}

Int get_Text_DictStringInt128(Text needle, DictStringInt128* restrict haystack) {
   dictGetterFn_Text(return haystack->c[i].value, return -1); // TODO throw exception
}

Int getOrDefault_DictStringInt128(
   Arr(Byte const) needle, Int defaultValue, DictStringInt128* restrict haystack
) {
   dictGetterFn(return haystack->c[i].value, return defaultValue);
}

Int getOrDefault_Text_DictStringInt128(
   Text needle, Int defaultValue, DictStringInt128* restrict haystack
) {
   Unt needleHash = hashOfText(needle);
   Unt ind = needleHash >> 25;
   Unt const end = haystack->dict[ind + 1];
   for (Unt i = haystack->dict[ind]; i < end; i++) {
      if (haystack->hashes[i] == needleHash 
            && haystack->c[i].key + needle.len < haystack->textLen
            && memcmp(haystack->text + haystack->c[i].key, needle.c, needle.len) == 0
      ) {
         return haystack->c[i].value;
      }
   }
   return defaultValue;
}

Int getKv_Text_DictStringInt128(
   OUT Unt* key, Text needle, DictStringInt128* restrict haystack
) {
   Unt needleHash = hashOfText(needle);
   Unt ind = needleHash >> 25;
   Unt const end = haystack->dict[ind + 1];
   for (Unt i = haystack->dict[ind]; i < end; i++) {
      if (haystack->hashes[i] == needleHash 
            && haystack->c[i].key + needle.len < haystack->textLen
            && memcmp(haystack->text + haystack->c[i].key, needle.c, needle.len) == 0
      ) {
         *key = haystack->c[i].key;
         return haystack->c[i].value;
      }
   }
   return 0;
}

#undef dictGetterFn

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
hash_init(EeSet *ht) {
   // This zeroes all "ht_" entries and all the "hi_key" in "smallArray".
   CLEAR_POINTER(ht);
   ht->array = ht->smallArray;
   ht->mask = HT_INIT_SIZE - 1;
}

// If "ht->flags" has HTFLAGS_FROZEN then give an error message using "command" and return TRUE.
int
check_hashtab_frozen(EeSet *ht, CS command) {
   if ((ht->flags & HTFLAGS_FROZEN) == 0)
      return FALSE;

   showErrFmtMsg(_(e_not_allowed_to_add_or_remove_entries_str), command);
      return TRUE;
}

// Free the array of a hash table.  Does not free the items it contains!
// If "ht" is not freed then you should call hash_init() next!
void
hash_clear(EeSet *ht) {
   if (ht->array != ht->smallArray)
      eeglFree(ht->array);
}

// Free the array of a hash table and all the keys it contains.  The keys must have been allocated.
// "off" is the offset from the start of the allocate memory to the location of the key (it's 
// always positive).
void
hash_clear_all(EeSet *ht, int off) {
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
// If the item was not found, then HASHITEM_EMPTY() is TRUE.  The pointer is then the place where
// the key would be added. WARNING: The returned pointer becomes invalid when the hashtable is 
// changed (adding, setting or removing an item)!
EeSetItem *
hash_find(EeSet* ht, Text key) {
   return hash_lookup(ht, key, calcHash(key));
}

// Like hash_find(), but caller computes "hash".
EeSetItem *
hash_lookup(EeSet *ht, Text key, Hash hash) {
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
hash_remove(EeSet *ht, EeSetItem *hi, CS command) {
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
hash_unlock(EeSet *ht) {
   --ht->ht_locked;
   (void)hash_may_resize(ht, 0);
}

// Shrink a hashtable when there is too much empty space. Grow a hashtable when there is not 
// enough empty space. Return OK or FAIL (overflow on size).
private int
hash_may_resize(
   EeSet* ht,
   int      minitems)      // minimal number of items
{
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
         mch_memmove(temparray, newarray, sizeof(temparray));
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
//{{{sha256

// FIPS-180-2 compliant SHA-256 implementation
// GPL by Christophe Devine, applies to older version.
// Modified for md5deep, in public domain.
// Modified For Vim, Mohsin Ahmed,
// (original link www.cs.albany.edu/~mosh no longer available)
// Mohsin Ahmed states this work is distributed under the VIM License or GPL,
// at your choice.
//
// Eegl specific notes:
// Functions exported by this file:
//  1. sha256_key() hashes the password to 64 bytes char string.
//  2. sha2_seed() generates a random header.
//  sha256_self_test() is implicitly called once.


#define GET_UINT32(n, b, i)          \
{                   \
    (n) = ( (UINT32)(b)[(i)    ] << 24)   \
   | ( (UINT32)(b)[(i) + 1] << 16)   \
   | ( (UINT32)(b)[(i) + 2] <<  8)   \
   | ( (UINT32)(b)[(i) + 3]   );  \
}

#define PUT_UINT32(n,b,i)        \
{                 \
    (b)[(i)    ] = (Byte)((n) >> 24);   \
    (b)[(i) + 1] = (Byte)((n) >> 16);   \
    (b)[(i) + 2] = (Byte)((n) >>  8);   \
    (b)[(i) + 3] = (Byte)((n)      );   \
}

void
sha256_start(ContextSha256 *ctx) {
   ctx->total[0] = 0;
   ctx->total[1] = 0;

   ctx->state[0] = 0x6A09E667;
   ctx->state[1] = 0xBB67AE85;
   ctx->state[2] = 0x3C6EF372;
   ctx->state[3] = 0xA54FF53A;
   ctx->state[4] = 0x510E527F;
   ctx->state[5] = 0x9B05688C;
   ctx->state[6] = 0x1F83D9AB;
   ctx->state[7] = 0x5BE0CD19;
}

private void
sha256_process(ContextSha256 *ctx, Byte data[64]) {
   UINT32 temp1, temp2, W[64];
   UINT32 A, B, C, D, EE, F, G, H;

   GET_UINT32(W[0],  data,  0);
   GET_UINT32(W[1],  data,  4);
   GET_UINT32(W[2],  data,  8);
   GET_UINT32(W[3],  data, 12);
   GET_UINT32(W[4],  data, 16);
   GET_UINT32(W[5],  data, 20);
   GET_UINT32(W[6],  data, 24);
   GET_UINT32(W[7],  data, 28);
   GET_UINT32(W[8],  data, 32);
   GET_UINT32(W[9],  data, 36);
   GET_UINT32(W[10], data, 40);
   GET_UINT32(W[11], data, 44);
   GET_UINT32(W[12], data, 48);
   GET_UINT32(W[13], data, 52);
   GET_UINT32(W[14], data, 56);
   GET_UINT32(W[15], data, 60);

#define  SHR(x, n) (((x) & 0xFFFFFFFF) >> (n))
#define ROTR(x, n) (SHR(x, n) | ((x) << (32 - (n))))

#define S0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^  SHR(x, 3))
#define S1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^  SHR(x, 10))

#define S2(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))

#define F0(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define F1(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))

#define R(t)            \
(               \
    W[t] = S1(W[(t) -  2]) + W[(t) -  7] +   \
      S0(W[(t) - 15]) + W[(t) - 16]   \
)

#define P(a,b,c,d,e,f,g,h,x,K)           \
{                    \
    temp1 = (h) + S3(e) + F1(e, f, g) + (K) + (x); \
    temp2 = S2(a) + F0(a, b, c);        \
    (d) += temp1; (h) = temp1 + temp2;        \
}

   A = ctx->state[0];
   B = ctx->state[1];
   C = ctx->state[2];
   D = ctx->state[3];
   EE = ctx->state[4];
   F = ctx->state[5];
   G = ctx->state[6];
   H = ctx->state[7];

   P( A, B, C, D, EE, F, G, H, W[ 0], 0x428A2F98);
   P( H, A, B, C, D, EE, F, G, W[ 1], 0x71374491);
   P( G, H, A, B, C, D, EE, F, W[ 2], 0xB5C0FBCF);
   P( F, G, H, A, B, C, D, EE, W[ 3], 0xE9B5DBA5);
   P( EE, F, G, H, A, B, C, D, W[ 4], 0x3956C25B);
   P( D, EE, F, G, H, A, B, C, W[ 5], 0x59F111F1);
   P( C, D, EE, F, G, H, A, B, W[ 6], 0x923F82A4);
   P( B, C, D, EE, F, G, H, A, W[ 7], 0xAB1C5ED5);
   P( A, B, C, D, EE, F, G, H, W[ 8], 0xD807AA98);
   P( H, A, B, C, D, EE, F, G, W[ 9], 0x12835B01);
   P( G, H, A, B, C, D, EE, F, W[10], 0x243185BE);
   P( F, G, H, A, B, C, D, EE, W[11], 0x550C7DC3);
   P( EE, F, G, H, A, B, C, D, W[12], 0x72BE5D74);
   P( D, EE, F, G, H, A, B, C, W[13], 0x80DEB1FE);
   P( C, D, EE, F, G, H, A, B, W[14], 0x9BDC06A7);
   P( B, C, D, EE, F, G, H, A, W[15], 0xC19BF174);
   P( A, B, C, D, EE, F, G, H, R(16), 0xE49B69C1);
   P( H, A, B, C, D, EE, F, G, R(17), 0xEFBE4786);
   P( G, H, A, B, C, D, EE, F, R(18), 0x0FC19DC6);
   P( F, G, H, A, B, C, D, EE, R(19), 0x240CA1CC);
   P( EE, F, G, H, A, B, C, D, R(20), 0x2DE92C6F);
   P( D, EE, F, G, H, A, B, C, R(21), 0x4A7484AA);
   P( C, D, EE, F, G, H, A, B, R(22), 0x5CB0A9DC);
   P( B, C, D, EE, F, G, H, A, R(23), 0x76F988DA);
   P( A, B, C, D, EE, F, G, H, R(24), 0x983E5152);
   P( H, A, B, C, D, EE, F, G, R(25), 0xA831C66D);
   P( G, H, A, B, C, D, EE, F, R(26), 0xB00327C8);
   P( F, G, H, A, B, C, D, EE, R(27), 0xBF597FC7);
   P( EE, F, G, H, A, B, C, D, R(28), 0xC6E00BF3);
   P( D, EE, F, G, H, A, B, C, R(29), 0xD5A79147);
   P( C, D, EE, F, G, H, A, B, R(30), 0x06CA6351);
   P( B, C, D, EE, F, G, H, A, R(31), 0x14292967);
   P( A, B, C, D, EE, F, G, H, R(32), 0x27B70A85);
   P( H, A, B, C, D, EE, F, G, R(33), 0x2E1B2138);
   P( G, H, A, B, C, D, EE, F, R(34), 0x4D2C6DFC);
   P( F, G, H, A, B, C, D, EE, R(35), 0x53380D13);
   P( EE, F, G, H, A, B, C, D, R(36), 0x650A7354);
   P( D, EE, F, G, H, A, B, C, R(37), 0x766A0ABB);
   P( C, D, EE, F, G, H, A, B, R(38), 0x81C2C92E);
   P( B, C, D, EE, F, G, H, A, R(39), 0x92722C85);
   P( A, B, C, D, EE, F, G, H, R(40), 0xA2BFE8A1);
   P( H, A, B, C, D, EE, F, G, R(41), 0xA81A664B);
   P( G, H, A, B, C, D, EE, F, R(42), 0xC24B8B70);
   P( F, G, H, A, B, C, D, EE, R(43), 0xC76C51A3);
   P( EE, F, G, H, A, B, C, D, R(44), 0xD192E819);
   P( D, EE, F, G, H, A, B, C, R(45), 0xD6990624);
   P( C, D, EE, F, G, H, A, B, R(46), 0xF40E3585);
   P( B, C, D, EE, F, G, H, A, R(47), 0x106AA070);
   P( A, B, C, D, EE, F, G, H, R(48), 0x19A4C116);
   P( H, A, B, C, D, EE, F, G, R(49), 0x1E376C08);
   P( G, H, A, B, C, D, EE, F, R(50), 0x2748774C);
   P( F, G, H, A, B, C, D, EE, R(51), 0x34B0BCB5);
   P( EE, F, G, H, A, B, C, D, R(52), 0x391C0CB3);
   P( D, EE, F, G, H, A, B, C, R(53), 0x4ED8AA4A);
   P( C, D, EE, F, G, H, A, B, R(54), 0x5B9CCA4F);
   P( B, C, D, EE, F, G, H, A, R(55), 0x682E6FF3);
   P( A, B, C, D, EE, F, G, H, R(56), 0x748F82EE);
   P( H, A, B, C, D, EE, F, G, R(57), 0x78A5636F);
   P( G, H, A, B, C, D, EE, F, R(58), 0x84C87814);
   P( F, G, H, A, B, C, D, EE, R(59), 0x8CC70208);
   P( EE, F, G, H, A, B, C, D, R(60), 0x90BEFFFA);
   P( D, EE, F, G, H, A, B, C, R(61), 0xA4506CEB);
   P( C, D, EE, F, G, H, A, B, R(62), 0xBEF9A3F7);
   P( B, C, D, EE, F, G, H, A, R(63), 0xC67178F2);

   ctx->state[0] += A;
   ctx->state[1] += B;
   ctx->state[2] += C;
   ctx->state[3] += D;
   ctx->state[4] += EE;
   ctx->state[5] += F;
   ctx->state[6] += G;
   ctx->state[7] += H;
}

void
sha256_update(ContextSha256 *ctx, CS input, UINT32 length) {
   UINT32 left, fill;

   if (length == 0)
      return;

   left = ctx->total[0] & 0x3F;
   fill = 64 - left;

   ctx->total[0] += length;
   ctx->total[0] &= 0xFFFFFFFF;

   if (ctx->total[0] < length)
      ctx->total[1]++;

   if (left && length >= fill) {
      memcpy((void *)(ctx->buffer + left), (void *)input, fill);
      sha256_process(ctx, ctx->buffer);
      length -= fill;
      input  += fill;
      left = 0;
   }

   while (length >= 64) {
      sha256_process(ctx, input);
      length -= 64;
      input  += 64;
   }

    if (length)
   memcpy((void *)(ctx->buffer + left), (void *)input, length);
}

private Byte sha256_padding[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void
sha256_finish(ContextSha256 *ctx, Byte digest[32]) {
    UINT32 last, padn;
    UINT32 high, low;
    Byte   msglen[8];

    high = (ctx->total[0] >> 29) | (ctx->total[1] <<  3);
    low  = (ctx->total[0] <<  3);

    PUT_UINT32(high, msglen, 0);
    PUT_UINT32(low,  msglen, 4);

    last = ctx->total[0] & 0x3F;
    padn = (last < 56) ? (56 - last) : (120 - last);

    sha256_update(ctx, sha256_padding, padn);
    sha256_update(ctx, msglen, 8);

    PUT_UINT32(ctx->state[0], digest,  0);
    PUT_UINT32(ctx->state[1], digest,  4);
    PUT_UINT32(ctx->state[2], digest,  8);
    PUT_UINT32(ctx->state[3], digest, 12);
    PUT_UINT32(ctx->state[4], digest, 16);
    PUT_UINT32(ctx->state[5], digest, 20);
    PUT_UINT32(ctx->state[6], digest, 24);
    PUT_UINT32(ctx->state[7], digest, 28);
}

// Return hex digest of "buf[buf_len]" in a static array.
// if "salt" is not NULL also do "salt[salt_len]".
CS
sha256_bytes(
   CS buf,
   int buf_len,
   CS salt,
   int salt_len)
{
   Byte        sha256sum[32];
   static Byte    hexit[65];
   int           j;
   ContextSha256 ctx;

   sha256_self_test();

   sha256_start(&ctx);
   sha256_update(&ctx, buf, buf_len);
   if (salt != NULL)
      sha256_update(&ctx, salt, salt_len);
   sha256_finish(&ctx, sha256sum);
   for (j = 0; j < 32; j++)
      sprintf((char *)hexit + j * 2, "%02x", sha256sum[j]);
   hexit[sizeof(hexit) - 1] = '\0';
   return hexit;
}

// Return sha256(buf) as 64 hex chars in static array.
CS
sha256_key(
   CS buf,
   CS salt,
   int salt_len
){
   // No passwd means don't encrypt
   if (buf == NULL || *buf == ZERO)
      return (CS)"";

   return sha256_bytes(buf, (int)STRLEN(buf), salt, salt_len);
}

// These are the standard FIPS-180-2 test vectors

private char *sha_self_test_msg[] = {
    "abc",
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
    NULL
};

private char *sha_self_test_vector[] = {
   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
   "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
   "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
};

// Perform a test on the SHA256 algorithm. Return FAIL or OK.
int
sha256_self_test(void) {
   int           i, j;
   char        output[65];
   ContextSha256 ctx;
   Byte        buf[1000];
   Byte        sha256sum[32];
   static int        failures = 0;
   Byte        *hexit;
   static int        sha256_self_tested = 0;

   if (sha256_self_tested > 0)
      return failures > 0 ? FAIL : OK;
   sha256_self_tested = 1;

   for (i = 0; i < 3; i++) {
      if (i < 2) {
         hexit = sha256_bytes((CS)sha_self_test_msg[i],
            (int)STRLEN(sha_self_test_msg[i]),
            NULL, 0
         );
         STRCPY(output, hexit);
      } else {
          sha256_start(&ctx);
          memset(buf, 'a', 1000);
          for (j = 0; j < 1000; j++)
         sha256_update(&ctx, (CS)buf, 1000);
          sha256_finish(&ctx, sha256sum);
          for (j = 0; j < 32; j++)
         sprintf(output + j * 2, "%02x", sha256sum[j]);
      }
      if (memcmp(output, sha_self_test_vector[i], 64)) {
          failures++;
          output[sizeof(output) - 1] = '\0';
          // printf("sha256_self_test %d failed %s\n", i, output);
      }
    }
    return failures > 0 ? FAIL : OK;
}

private unsigned int
get_some_time(void) {
# ifdef HAVE_GETTIMEOFDAY
    TimeVal tv;

    // Using usec makes it less predictable.
    gettimeofday(&tv, NULL);
    return (unsigned int)(tv.tv_sec + tv.tv_usec);
# else
    return (unsigned int)time(NULL);
# endif
}

// Fill "header[header_len]" with random_data. Also "salt[salt_len]" when "salt" is not NULL.
void
sha2_seed(
   CS header,
   int    header_len,
   CS salt,
   int    salt_len)
{
   int           i;
   static Byte    random_data[1000];
   Byte        sha256sum[32];
   ContextSha256 ctx;

   srand(get_some_time());

   for (i = 0; i < (int)sizeof(random_data) - 1; i++)
      random_data[i] = (Byte)((get_some_time() ^ rand()) & 0xff);
   sha256_start(&ctx);
   sha256_update(&ctx, (CS)random_data, sizeof(random_data));
   sha256_finish(&ctx, sha256sum);

   // put first block into header.
   for (i = 0; i < header_len; i++)
      header[i] = sha256sum[i % sizeof(sha256sum)];

   // put remaining block into salt.
   if (salt) {
      for (i = 0; i < salt_len; i++)
          salt[i] = sha256sum[(i + header_len) % sizeof(sha256sum)];
   }
}

//}}}
//{{{doubly-linked list

// Iterative merge sort for doubly linked list.
// O(NlogN) worst case, and stable.
//  - The list is divided into blocks of increasing size (1, 2, 4, 8, ...).
//  - Each pair of blocks is merged in sorted order.
//  - Merged blocks are reconnected to build the sorted list.
void *
mergesort_list(
   void *head,
   void *(*get_next)(void *),
   void (*set_next)(void *, void *),
   void *(*get_prev)(void *),
   void (*set_prev)(void *, void *),
   int (*compare)(const void *, const void *)
){
   if (!head || !get_next(head))
      return head;

    // Count length
   int       n = 0;
   void*   curr = head;
   while (curr) {
      n++;
      curr = get_next(curr);
   }

   int   size;
   for (size = 1; size < n; size *= 2) {
      void*   new_head = NULL;
      void*   tail = NULL;
      curr = head;

      while (curr) {
          // Split two runs
          void    *left = curr;
          void    *right = left;
          int       i;
          for (i = 0; i < size && right; ++i)
         right = get_next(right);

          void    *next = right;
          for (i = 0; i < size && next; ++i)
         next = get_next(next);

          // Break links
          void    *l_end = right ? get_prev(right) : NULL;
          if (l_end)
         set_next(l_end, NULL);
          if (right)
         set_prev(right, NULL);

         void    *r_end = next ? get_prev(next) : NULL;
         if (r_end)
            set_next(r_end, NULL);
         if (next)
            set_prev(next, NULL);

         // Merge
         void    *merged = NULL;
         void    *merged_tail = NULL;

         while (left || right) {
            void   *chosen = NULL;
            if (!left) {
                chosen = right;
                right = get_next(right);
            } ei (!right) {
                chosen = left;
                left = get_next(left);
            } ei (compare(left, right) <= 0) {
                chosen = left;
                left = get_next(left);
            } else {
                chosen = right;
                right = get_next(right);
            }

            if (merged_tail) {
                set_next(merged_tail, chosen);
                set_prev(chosen, merged_tail);
                merged_tail = chosen;
            } else {
                merged = merged_tail = chosen;
                set_prev(chosen, NULL);
            }
         }

          // Connect to full list
         if (!new_head)
            new_head = merged;
         else {
            set_next(tail, merged);
            set_prev(merged, tail);
         }

         // Move tail to end
         while (get_next(merged_tail))
            merged_tail = get_next(merged_tail);
         tail = merged_tail;

         curr = next;
      }

      head = new_head;
   }

   return head;
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

//Return TRUE if "type" is NULL, any or unknown.
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
freeVar(Var *varp) {
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
clearVar(Var *varp) {
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
initVarToNull(OUT Var *varp) {
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
//caller of incompatible types: set *denote to TRUE if "denote" is not NULL or return -1 otherwise.
Long
tv_get_number(Var *varp) {
   return varGetNumberChk(varp, null);   // return 0L on error
}

Long
varGetNumberChk(Var* varp, OUT Boole* denote) {
   return convertToBoolOrNumber(varp, OUT denote);
}

//Get the boolean value of "varp". This is like varGetNumberChk(),
Boole
tv_get_bool(Var *varp) {
   return convertToBoolOrNumber(varp, NULL);
}

private double
convertToDouble(Var *varp, OUT Boole* error) {
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
tv_get_float(Var *varp) {
    return convertToDouble(varp, NULL);
}

// Give an error and return FAIL unless "args[idx]" is unknown
int
check_for_unknown_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_UNKNOWN) {
   showErrFmtMsg(_(e_too_many_arguments), idx + 1);
   return FAIL;
    }
    return OK;
}

// Give an error and return FAIL unless "args[idx]" is a string.
int
check_for_string_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_STRING) {
      showErrFmtMsg(_(e_string_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Give an error and return FAIL unless "args[idx]" is a non-empty string.
int
check_for_nonempty_string_arg(Var *args, int idx) {
   if (check_for_string_arg(args, idx) == FAIL)
      return FAIL;
   if (args[idx].string == NULL || *args[idx].string == ZERO) {
      showErrFmtMsg(_(e_non_empty_string_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

/*
 * Check for an optional string argument at 'idx'
 */
int
check_for_opt_string_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN || check_for_string_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a number.
int
check_for_number_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional number argument at 'idx'
int
check_for_opt_number_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_number_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a float or a number.
int
check_for_float_or_nr_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_FLOAT && args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_float_or_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a bool.
int
check_for_bool_arg(Var *args, int idx) {
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
check_for_opt_bool_arg(Var *args, int idx) {
   if (args[idx].tag == VAR_UNKNOWN)
      return OK;
   return check_for_bool_arg(args, idx);
}

//Check for an optional bool or number argument at 'idx'. Return FAIL if the type is wrong.
int
check_for_opt_bool_or_number_arg(Var *args, int idx) {
   if (args[idx].tag == VAR_UNKNOWN)
      return OK;
   return checkVarsForBoolOrNumber(args, idx);
}

//Give an error and return FAIL unless "args[idx]" is a blob.
int
check_for_blob_arg(Var *args, int idx) {
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
confirmVarIsNonnullList(Var *args, int idx) {
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
confirmVarIsOptionalList(Var *args, int idx) {
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
check_for_opt_nonnull_dict_arg(Var *args, int idx) {
    return (args[idx].tag == VAR_UNKNOWN
       || check_for_nonnull_dict_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a channel or a job.
int
check_for_chan_or_job_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_CHANNEL && args[idx].tag != VAR_JOB) {
      showErrFmtMsg(_(e_chan_or_job_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is an optional channel or a job.
int
check_for_opt_chan_or_job_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_chan_or_job_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a job.
int
check_for_job_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_JOB) {
      showErrFmtMsg(_(e_job_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

// Check for an optional job argument at 'idx'.
int
check_for_opt_job_arg(Var *args, int idx) {
    return (args[idx].tag == VAR_UNKNOWN
       || check_for_job_arg(args, idx) != FAIL) ? OK : FAIL;
}

// Give an error and return FAIL unless "args[idx]" is a string or a number.
int
check_for_string_or_number_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_NUMBER) {
      showErrFmtMsg(_(e_string_or_number_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional string or number argument at 'idx'.
int
check_for_opt_string_or_number_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_string_or_number_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a buffer number.
//Book number can be a number or a string.
int
check_for_buffer_arg(Var *args, int idx) {
   return check_for_string_or_number_arg(args, idx);
}

//Check for an optional buffer argument at 'idx'
int
check_for_opt_buffer_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_buffer_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a line number.
//Line number can be a number or a string.
int
check_for_lnum_arg(Var *args, int idx) {
   return check_for_string_or_number_arg(args, idx);
}

//Check for an optional line number argument at 'idx'
int
check_for_opt_lnum_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_lnum_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a string or a blob.
int
check_for_string_or_blob_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_string_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a string or a list.
int
check_for_string_or_list_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_LIST) {
      showErrFmtMsg(_(e_string_or_list_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a string, a list or a blob.
private int
check_for_string_or_list_or_blob_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_LIST && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_string_list_tuple_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Check for an optional string or list argument at 'idx'
int
check_for_opt_string_or_list_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_string_or_list_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a string or a dict.
int
check_for_string_or_dict_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_STRING && args[idx].tag != VAR_BAG) {
   showErrFmtMsg(_(e_string_or_dict_required_for_argument_nr), idx + 1);
   return FAIL;
    }
    return OK;
}

// Give an error and return FAIL unless "args[idx]" is a string or a number or a list.
int
check_for_string_or_number_or_list_arg(Var *args, int idx) {
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
check_for_opt_string_or_number_or_list_arg(Var *args, int idx) {
   return (args[idx].tag == VAR_UNKNOWN
       || check_for_string_or_number_or_list_arg(args, idx) != FAIL) ? OK : FAIL;
}

//Give an error and return FAIL unless "args[idx]" is a string, a number, a list or a blob
int
check_for_repeat_func_arg(Var *args, int idx) {
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
check_for_string_list_or_dict_arg(Var *args, int idx) {
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
check_for_string_or_func_arg(Var *args, int idx) {
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
check_for_list_or_blob_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_LIST && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_list_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list.
int
check_for_list_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_LIST) {
      showErrFmtMsg(_(e_list_or_tuple_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list or a blob.
int
check_for_list_or_or_blob_arg(Var *args, int idx) {
   if (args[idx].tag != VAR_LIST && args[idx].tag != VAR_BLOB) {
      showErrFmtMsg(_(e_list_or_tuple_or_blob_required_for_argument_nr), idx + 1);
      return FAIL;
   }
   return OK;
}

//Give an error and return FAIL unless "args[idx]" is a list or a dict
int
check_for_list_or_dict_arg(Var *args, int idx) {
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
check_for_list_or_dict_or_blob_arg(Var *args, int idx) {
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
check_for_list_dict_blob_or_string_arg(Var *args, int idx) {
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
check_for_opt_buffer_or_dict_arg(Var *args, int idx) {
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
tv_get_string(Var *varp) {
   static Byte mybuf[NUMBUFLEN];
   return tv_get_string_buf(varp, mybuf);
}

//Like tv_get_string() but don't allow number to string conversion for Vim9.
CS
tv_get_string_strict(Var *varp) {
   static Byte   mybuf[NUMBUFLEN];
   Byte* res =  convertVarToString_strict(varp, mybuf, FALSE);

   return res != NULL ? res : (CS)"";
}

CS
tv_get_string_buf(Var *varp, CS buf) {
    CS res = convertVarToString(varp, buf);
    return res ? res : Em;
}

//Careful: This uses a single, static buffer. YOU CAN ONLY USE IT ONCE!
CS
convertVarToStringSingleUse(Var *varp) {
   static Byte   mybuf[NUMBUFLEN];
   return convertVarToString(varp, mybuf);
}

CS
convertVarToString(Var *varp, CS buf) {
   return convertVarToString_strict(varp, buf, FALSE);
}

CS
convertVarToString_strict(Var *varp, CS buf, int strict) {
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
      return Em;
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
Arr(Byte)
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

//Return TRUE if typeval "tv" and its value are set to be locked (immutable).
//Also give an error message, using "name" or _("name") when use_gettext is TRUE.
int
tv_check_lock(Var *tv, Text name, Boole use_gettext) {
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
copy_tv(OUT Var* to, Var *from) {
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
      //For "is" a different type always means FALSE, for "isnot" it means TRUE.
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
   n1 = FALSE;
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


//Compare v:null with another type.  Return TRUE if the value is NULL.
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
    return FALSE;
}

//Compare "tv1" to "tv2" as blobs according to "type".
//Put the result, false or true, in "res".
//Return FAIL and give an error message when the comparison can't be done.
int
typval_compare_blob(
   Var    *tv1,
   Var    *tv2,
   ExprType  type,
   int       *res
) {
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
   Var    *tv1,
   Var    *tv2,
   ExprType  type,
   int       ic,
   int       *res
) {
   int       val;

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
   Var    *tv1,
   Var    *tv2,
   ExprType  type,
   int       ic,
   int       *res
) {
   int       val = 0;

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
         val = FALSE;
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
   Var    *tv1,
   Var    *tv2,
   ExprType  type,
   Boole ignoreCase,
   int       *res
) {
   int      i = 0;
   int      val = FALSE;
   Byte   *s1, *s2;
   Byte   buf1[NUMBUFLEN], buf2[NUMBUFLEN];

   s1 = tv_get_string_buf(tv1, buf1);
   s2 = tv_get_string_buf(tv2, buf2);
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
//When "quotes" is TRUE add quotes to a string. Return an allocated string.
CS
typval_tostring(Var *arg, int quotes) {
   Byte   *tofree;
   Byte   numbuf[NUMBUFLEN];
   Byte   *ret = NULL;

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

//Return TRUE if internal var is locked: Either that value is locked itself
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
   Byte   *s1, *s2;
   Bag   *d1, *d2;
   int      a1, a2;
   int      i;

   // empty and NULL function name considered the same
   s1 = tv1->tag == VAR_FUNC ? tv1->string : partial_name(tv1->partial);
   if (s1 != NULL && *s1 == ZERO)
      s1 = NULL;
   s2 = tv2->tag == VAR_FUNC ? tv2->string
                  : partial_name(tv2->partial);
   if (s2 != NULL && *s2 == ZERO)
   s2 = NULL;
   if (s1 == NULL || s2 == NULL) {
      if (s1 != s2)
          return FALSE;
   } ei (STRCMP(s1, s2) != 0)
      return FALSE;

   // empty dict and NULL dict is different
   d1 = tv1->tag == VAR_FUNC ? NULL : tv1->partial->self;
   d2 = tv2->tag == VAR_FUNC ? NULL : tv2->partial->self;
   if (!d1 || !d2) {
      if (d1 != d2)
         return FALSE;
   } ei (!bagEqual(d1, d2, ic))
      return FALSE;

   // empty list and no list considered the same
   a1 = tv1->tag == VAR_FUNC ? 0 : tv1->partial->argc;
   a2 = tv2->tag == VAR_FUNC ? 0 : tv2->partial->argc;
   if (a1 != a2)
      return FALSE;
   for (i = 0; i < a1; ++i) {
      if (!tv_equal(tv1->partial->argv + i, tv2->partial->argv + i, ic))
         return FALSE;
   } 

   return TRUE;
}

//Return TRUE if "tv1" and "tv2" have the same value.
//Compares the items just like "==" would compare them, but strings and
//numbers are different.  Floats and numbers are also different.
int
tv_equal(Var *tv1, Var *tv2, int ic) {      // ignore case
   Byte   buf1[NUMBUFLEN], buf2[NUMBUFLEN];
   Byte   *s1, *s2;
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
      return TRUE;
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
      return FALSE;

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
   return FALSE;
}

//Get an option value.
//"arg" points to the '&' or '+' before the option name.
//"arg" is advanced to character after the option name.
//Return OK or FAIL.
int
eval_option(
   Byte   **arg,
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
eval_number(
   Byte       **arg,
   Var    *returnVar,
   int       evaluate,
   int       want_string
) {
   int get_float = FALSE;

   // We accept a float when the format matches
   // "[0-9]\+\.[0-9]\+\([eE][+-]\?[0-9]\+\)\?".  This is very
   // strict to avoid backwards compatibility problems.
   // The leading digit can be omitted.
   // Don't look for a float after the "." operator, so that ":let vers = 1.2.3" doesn't fail.
   Byte   *p;
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
      get_float = TRUE;
      p = skipdigits(p + 2);
      if (*p == 'e' || *p == 'E') {
         ++p;
         if (*p == '-' || *p == '+')
            ++p;
         if (!eeIsDigit(*p))
            get_float = FALSE;
         else
            p = skipdigits(p + 1);
      }
      if (ASCII_ISALPHA(*p) || *p == '.')
         get_float = FALSE;
   }
   if (get_float) {
      double   f;

      *arg += string2float(*arg, &f, TRUE);
      if (evaluate) {
          returnVar->tag = VAR_FLOAT;
          returnVar->floatt = f;
      }
   } ei (**arg == '0' && ((*arg)[1] == 'z' || (*arg)[1] == 'Z')) {
      Byte  *bp;
      Blob  *blob = NULL;

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
      if (blob != NULL)
          returnVar_blob_set(returnVar, blob);
      *arg = bp;
   } else {
      Long   n;

      // decimal or hex number
      int len;
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
   Byte* numbuf,
   int copyID)
{
   return echo_string_core(tv, tofree, numbuf, copyID, FALSE, TRUE, FALSE);
}

//Get the value of an environment variable.
//"arg" is pointing to the '$'.  It is advanced to after the name.
//If the environment variable was not set, silently assume it is empty.
//Return FAIL if the name is invalid.
int
eval_env_var(Byte **arg, Var *returnVar, int evaluate) {
   Byte   *string = NULL;
   int      cc;
   int      mustfree = FALSE;

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
         string = expand_env_save(name - 1);
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
tv_get_lnum(Var *argvars) {
   int      anyEmsgG_before = anyEmsgG;

   LineNr lnum = (LineNr)varGetNumberChk(&argvars[0], NULL);
   if (lnum <= 0 && anyEmsgG_before == anyEmsgG && argvars[0].tag != VAR_NUMBER) {
      int   fnum;
      // no valid number, try using arg like line()
      Pos* fp = var2fpos(&argvars[0], TRUE, &fnum, FALSE);
      if (fp)
          lnum = fp->lnum;
   }
   return lnum;
}

//Get the lnum from the first argument.
//Also accepts "$", then "book" is used. Return 0 on error.
LineNr
tv_get_lnum_buf(Var *argvars, Book* book) {
   if (argvars[0].tag == VAR_STRING
          && argvars[0].string != NULL
          && argvars[0].string[0] == '$'
          && argvars[0].string[1] == ZERO
          && book)
      return book->mem.lineCount;
   return (LineNr)varGetNumberChk(&argvars[0], NULL);
}

//Get book by number or pattern.
Book *
daGetBook(Var *tv, Boole curtab_only) {
   CS name = tv->string;

   if (tv->tag == VAR_NUMBER)
      return bookFindFileByBookNr((int)tv->number);
   if (tv->tag != VAR_STRING)
      return NULL;
   if (name == NULL || *name == ZERO)
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
daGetBookFromArg(Var *tv) {
   ++emsg_off;
   Book* book = daGetBook(tv, FALSE);
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
      return FALSE;
   if (type1->tag != type2->tag)
      return FALSE;
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
       if (!equal_type(type1->member, type2->member, flags)
          || type1->argCount != type2->argCount)
      return FALSE;
       if (type1->argCount < 0
            || type1->args == NULL || type2->args == NULL)
      return TRUE;
       for (i = 0; i < type1->argCount; ++i)
      if ((flags & ETYPE_ARG_UNKNOWN) == 0
         && !equal_type(type1->args[i], type2->args[i],
                           flags))
          return FALSE;
       return TRUE;
    }
    return TRUE;
}

ExprType
get_compare_type(CS p, int *len, int *type_is) {
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
             *type_is = TRUE;
         }
          }
          break;
    }
    return type;
}

//Return TRUE when "tv" is not falsy: non-zero, non-empty string, non-empty
//list, etc.  Mostly like what JavaScript does, except that empty list and
//empty dictionary are FALSE.
int
tv2bool(Var *tv) {
   switch (tv->tag) {
   case VAR_NUMBER:
       return tv->number != 0;
   case VAR_FLOAT:
       return tv->floatt != 0.0;
   case VAR_PARTIAL:
       return tv->partial != NULL;
   case VAR_FUNC:
   case VAR_STRING:
       return tv->string != NULL && *tv->string != ZERO;
   case VAR_LIST:
       return tv->list != NULL && tv->list->len > 0;
   case VAR_BAG:
       return tv->bag != NULL && tv->bag->hashTable.count > 0;
   case VAR_BOOL:
   case VAR_SPECIAL:
       return tv->number == VVAL_TRUE ? TRUE : FALSE;
   case VAR_JOB:
       return tv->job != NULL;
   case VAR_CHANNEL:
       return tv->channel != NULL;
   case VAR_BLOB:
       return tv->blob != NULL && tv->blob->c.len > 0;
   case VAR_UNKNOWN:
   case VAR_ANY:
   case VAR_VOID:
       break;
   }
   return FALSE;
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
allocReturnDict(Var *returnVar) {
   Bag* b = allocBag_lock(0);
   returnVar_dict_set(returnVar, b);
}

// Set a dictionary as the return value
void
returnVar_dict_set(Var *returnVar, Bag *d) {
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
bagAdd(Bag *b, DictItem *item) {
   if (dict_wrong_func_name(b, &item->c, textOfDi(item)))
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
bagAddNumber(Bag *d, CS key, Long nr) {
   return bagAddNumber_special(d, key, nr, VAR_NUMBER);
}

// Add a special entry to a Bag. FAIL when out of memory or when key already exists.
int
bagAdd_bool(Bag *d, CS key, Long nr) {
   return bagAddNumber_special(d, key, nr, VAR_BOOL);
}

// Add a string entry to Bag. Return FAIL when out of memory or when key already exists.
int
bagAddString(Bag *d, CS key, CS str) {
   return bagAddString_len(d, key, str, -1);
}

// Add a string entry to Bag. "str" will be copied to allocated memory.
// When "len" is -1 use the whole string, otherwise only this many bytes.
// Return FAIL when out of memory and when key already exists.
int
bagAddString_len(Bag *d, CS key, CS str, int len) {
   DictItem   *item;
   Byte   *val = NULL;

   item = dictitem_alloc(mbText(key));
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
bagAddList(Bag *d, CS key, List *list) {
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
bagAddVar(Bag* b, CS key, Var *tv) {
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
bagAddCallback(Bag *d, CS key, Callback *cb) {
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
bagAddFn(Bag *d, CS key, UserFunc *fp) {
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
bagIterateStart(Var *var, DictIterator *iter) {
   if (var->tag != VAR_BAG || var->bag == NULL)
      iter->dit_todo = 0;
   else {
      Bag   *d = var->bag;

      iter->dit_todo = d->hashTable.count;
      iter->dit_hi = d->hashTable.array;
   }
}

// Iterate over the items referred to by "iter". It should be initialized with bagIterateStart().
// Return a pointer to the key. "*tv_result" is set to point to the value for that key.
// If there are no more items, NULL is returned.
CS
bagIterateNext(DictIterator *iter, Var **tv_result) {
   DictItem   *di;
   Byte      *result;

   if (iter->dit_todo == 0)
      return NULL;

   while (HASHITEM_EMPTY(iter->dit_hi))
      ++iter->dit_hi;

   di = HI2DI(iter->dit_hi);
   result = di->key;
   *tv_result = &di->c;

   --iter->dit_todo;
   ++iter->dit_hi;
   return result;
}

// Add a dict entry to dictionary "d".
// Return FAIL when out of memory and when key already exists.
int
bagAddBag(Bag *d, CS key, Bag *dict) {
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
bagSize(Bag *d) {
   return d ? (Long)d->hashTable.count : 0L;
}

// Find item "key[len]" in Dictionary "d". If "len" is negative use strlen(key). NULL when not found
DictItem *
bagFind(Bag *b, Text const key) {
#define AKEYLEN 200
   Byte   buf[AKEYLEN];
   CS akey;
   CS tofree = NULL;
   EeSetItem   *hi;

   if (!b)
      return NULL;
   if (key.len >= AKEYLEN) {
      tofree = akey = copySubstr(key.c, key.len);
   } else {
      // Avoid a malloc/free by using buf[].
      copySubstrToAllocation(buf, key);
      akey = buf;
   }

   hi = hash_find(&b->hashTable, (Text){.c = akey, .len = key.len});
   eeglFree(tofree);
   if (HASHITEM_EMPTY(hi))
      return NULL;
   return HI2DI(hi);
}

// Return TRUE if "key" is present in Dictionary "d".
int
bagHasKey(Bag* b, Text key) {
   return bagFind(b, key) != NULL;
}

// Get a Var item from a dictionary and copy it into "returnVar".
// Return FAIL if the entry doesn't exist or out of memory.
int
bagGetVar(Bag *d, Text key, Var *returnVar) {
   DictItem* di = bagFind(d, key);
   if (!di)
      return FAIL;
   copy_tv(OUT returnVar, &di->c);
   return OK;
}

//Get a string item from a dictionary. When "save" is TRUE allocate memory for it. When FALSE 
//a shared buffer is used, can only be used once! Return NULL if the entry doesn't exist or out 
//of memory.
CS
bagGetString(Bag *d, Text key, Boole save) {
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
bagToString(Var *tv, int copyID, int restore_copyID) {
   ArrayList   ga;
   Boole first = true;
   Byte numbuf[NUMBUFLEN];
   EeSetItem   *hi;
   Byte* s;
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
            first = FALSE;
         else
            ga_concat(&ga, S", ");

         CS tofree = string_quote(hi->hi_key, FALSE);
         if (tofree) {
            ga_concat(&ga, tofree);
            eeglFree(tofree);
         }
         ga_concat(&ga, S": ");
         s = echo_string_core(&HI2DI(hi)->c, &tofree, numbuf, copyID, FALSE, restore_copyID, TRUE);
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

// Advance over a literal key, including "-".  If the first character is not a
// literal key character then "key" is returned.
private CS
skip_literal_key(CS key) {
   CS p;

   for (p = key; ASCII_ISALNUM(*p) || *p == '_' || *p == '-'; ++p)
   ;
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
//"literal" is TRUE for #{key: val}
//Return OK or FAIL, or NOTDONE for {expr}.
int
bagEval(OUT CS* arg, Var *returnVar, EvalCtx *evalarg, int literal) {
   int evaluate = evalarg == NULL ? FALSE : (evalarg->eval_flags & EVAL_EVALUATE);
   Bag   *d = NULL;
   Var   tvkey;
   Var   tv;
   DictItem   *item;
   Byte   *curly_expr = skipwhite(*arg + 1);
   Byte   buf[NUMBUFLEN];
   int      had_comma;

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
            tvkey.string = typval_tostring(&tvkey, TRUE);
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
bagEvalLiteral(OUT CS* arg, Var *returnVar, EvalCtx *evalarg) {
   int      ret = OK;

   if ((*arg)[1] == '{') {
      ++*arg;
      ret = bagEval(OUT arg, returnVar, evalarg, TRUE);
   } else
      ret = NOTDONE;

   return ret;
}

// Make a copy of a Dictionary item.
private DictItem *
dictitem_copy(DictItem *org) {
   Unt   len = STRLEN(org->key);
   DictItem* di = alloc(offsetof(DictItem, key) + len + 1);

   mch_memmove(di->key, org->key, len + 1);
   di->flags = DI_FLAGS_ALLOC;
   copy_tv(OUT &di->c, &org->c);
   return di;
}

// Go over all entries in "d2" and add them to "d1".
// When "action" is "error" then a duplicate key is an error.
// When "action" is "force" then a duplicate key is overwritten.
// When "action" is "move" then move items instead of copying.
// Otherwise duplicate keys are ignored ("action" is "keep").
// "func_name" is used for reporting where an error occurred.
void
bagExtend(Bag *d1, Bag *d2, CS action) {
   DictItem   *di1;
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
            if (dict_wrong_func_name(d1, &HI2DI(hi2)->c, textOfItem(hi2)))
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
bagLookup(EeSetItem *hi) {
   return HI2DI(hi);
}

// Return TRUE when two bags have exactly the same key/values.
int
bagEqual(Bag* d1, Bag* d2, int ic) {      // ignore case for strings
   EeSetItem   *hi;
   DictItem   *item2;
   int      todo;

   if (d1 == d2)
      return TRUE;
   if (bagSize(d1) != bagSize(d2))
      return FALSE;
   if (bagSize(d1) == 0)
      // empty and NULL dicts are considered equal
      return TRUE;
   if (d1 == NULL || d2 == NULL)
      return FALSE;

   todo = (int)d1->hashTable.count;
   FOR_ALL_HASHTAB_ITEMS(&d1->hashTable, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
          item2 = bagFind(d2, textOfItem(hi));
          if (item2 == NULL)
         return FALSE;
          if (!tv_equal(&HI2DI(hi)->c, &item2->c, ic))
         return FALSE;
          --todo;
      }
   }
   return TRUE;
}

// Count the number of times item "needle" occurs in Bag "d". Case is ignored if "ic" is TRUE.
long
bagCount(Bag *d, Var *needle, int ic) {
   if (d == NULL)
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
// resulting Bag in "returnVar".  "is_new" is TRUE for extendnew().
void
bagExtend_func(
   Var   *argvars,
   CS arg_errmsg,
   int      is_new,
   Var   *returnVar)
{
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
      d1 = dict_copy(d1, FALSE, TRUE, get_copyID());
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
      returnVar->lock = FALSE;
   } else
      copy_tv(OUT returnVar, &argvars[0]);
}

// Implementation of map(), filter(), foreach() for a Bag.  Apply "expr" to
// every item in Bag "d" and return the result in "returnVar".
void
bagFilterMap(
   Bag      *d,
   FilterMap   filtermap,
   CS arg_errmsg,
   Var   *expr,
   Var   *returnVar
) {
   Bag   *d_ret = NULL;
   EeSet   *ht;
   EeSetItem   *hi;
   DictItem   *di;
   int      todo;
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
   todo = (int)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         int      r;

         --todo;
         di = HI2DI(hi);
         Text errMsg = mbText(arg_errmsg);
         if (filtermap == FILTERMAP_MAP
                && (value_check_lock(di->c.lock, errMsg, true)
                  || var_check_ro(di->flags, errMsg, true)))
            break;
         set_EeglVar_string(VV_KEY, di->key, -1);
         r = filter_map_one(&di->c, expr, filtermap, &newtv, &rem);
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
bagRemove(Var *argvars, Var *returnVar, CS arg_errmsg) {
   if (argvars[2].tag != VAR_UNKNOWN) {
      showErrFmtMsg(_(e_too_many_arguments_for_function_str), "remove()");
      return;
   }

   Bag* b = argvars[0].bag;
   if (!b || value_check_lock(b->lock, mbText(arg_errmsg), TRUE))
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
bagToList(Var *argvars, Var *returnVar, dict2List what) {
   List   *l2;
   DictItem   *di;
   EeSetItem   *hi;
   ListItem   *li;
   int      todo;

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

   todo = (int)d->hashTable.count;
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
f_items(Var *argvars, Var *returnVar) {
   if (argvars[0].tag == VAR_STRING)
      string2items(argvars, returnVar);
   ei (argvars[0].tag == VAR_LIST)
      list2items(argvars, returnVar);
   else
      bagToList(argvars, returnVar, DICT2LIST_ITEMS);
}

void
f_keys(Var *argvars, Var *returnVar) {
   bagToList(argvars, returnVar, DICT2LIST_KEYS);
}

void
f_values(Var *argvars, Var *returnVar) {
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
f_has_key(Var *argvars, Var *returnVar) {
   if (check_for_dict_arg(argvars, 0) == FAIL || argvars[0].bag == NULL)
      return;

   returnVar->number = bagHasKey(argvars[0].bag, mbText(tv_get_string(&argvars[1])));
}

//Return the slice "str[first : last]" using character indexes.  Composing
//characters are included. "exclusive" is TRUE for slice().
//Return NULL when the result is empty.
CS
string_slice(Byte *str, Long first, Long last, int exclusive) {
   if (!str)
      return NULL;
   Unt slen = STRLEN(str);
   long start_byte = char_idx2byte(str, slen, first);
   if (start_byte < 0)
      start_byte = 0; // first index very negative: use zero
   long   end_byte;
   if ((last == -1 && !exclusive) || last == VARNUM_MAX)
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


// Go through the list of dicts and free items without the copyID. TRUE if anything was freed.
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
dict_free(Bag *d) {
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
   mch_memmove(di->key, value.c, value.len + 1);
   di->flags = DI_FLAGS_ALLOC;
   di->c.lock = 0;
   di->c.tag = VAR_UNKNOWN;
   return di;
}

//Remove item "item" from Bag "bag" and free it.
//"command" is used for the error message when the hashtab if frozen.
void
dictitem_remove(Bag *bag, DictItem *item, CS command) {
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

// Make a copy of dict "d".  Shallow if "deep" is FALSE. The refcount of the new dict is set to 1.
// See item_copy() for "top" and "copyID". Return NULL when out of memory.
Bag *
dict_copy(Bag *orig, int deep, int top, int copyID) {
   if (!orig)
      return NULL;

   Bag* copy = allocBag();
      
   DictItem   *di;
   EeSetItem   *hi;

   if (copyID != 0) {
      orig->copyId = copyID;
      orig->dv_copydict = copy;
   }
   copy->ty = (orig->ty == NULL || top || deep) ? null : alloc_type(orig->ty);

   int todo = (int)orig->hashTable.count;
   for (hi = orig->hashTable.array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
          --todo;

         di = dictitem_alloc(textOfItem(hi));
         if (deep) {
            if (item_copy(&HI2DI(hi)->c, &di->c, deep, FALSE, copyID) == FAIL) {
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
// If the name is wrong give an error message and return TRUE.
int
dict_wrong_func_name(Bag *b, Var *tv, Text name) {
   return (b == get_globvar_dict() || &b->hashTable == get_funccal_local_ht())
       && (tv->tag == VAR_FUNC || tv->tag == VAR_PARTIAL)
       && var_wrong_func_name(name, TRUE);
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
json_encode_gap(ArrayList *gap, Var *val, int options) {
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
json_encode(Var *val, int options) {
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
json_encode_nr_expr(int nr, Var *val, int options) {
   Var   listtv;
   Var   nrtv;
   ArrayList   ga;

   nrtv.tag = VAR_NUMBER;
   nrtv.number = nr;
   allocReturnList(&listtv);
   if (list_append_tv(listtv.list, &nrtv) == FAIL
          || list_append_tv(listtv.list, val) == FAIL) {
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
json_encode_lsp_msg(Var *val) {
   ArrayList   ga;
   ArrayList   lspga;

   ga_init2(&ga, 1, 4000);
   if (json_encode_gap(&ga, val, 0) == FAIL)
      return NULL;
   ga_append(&ga, ZERO);

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
write_string(ArrayList *gap, CS str) {
   Byte   *res = str;
   Unt      c;

   if (!res) {
      ga_concat(gap, (CS)"\"\"");
      return;
   }

   ga_append(gap, '"');
   // `from` is the beginning of a sequence of bytes we can directly copy from
   // the input string, avoiding the overhead associated to decoding/encoding them.
   CS from = res;
   Byte   numbuf[NUMBUFLEN];
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
   Byte   numbuf[NUMBUFLEN];
   Byte   *res;
   Blob   *b;
   List   *l;
   Bag   *d;
   int      i;

   switch (val->tag) {
   case VAR_BOOL:
      switch ((long)val->number) {
         case VVAL_FALSE: ga_concat(gap, (CS)"false"); break;
         case VVAL_TRUE: ga_concat(gap, (CS)"true"); break;
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
         ga_concat(gap, (CS)"[]");
      else {
         ga_append(gap, '[');
         for (i = 0; i < b->c.len; i++) {
            if (i > 0)
               ga_concat(gap, (CS)",");
            eeSnprintf(numbuf, NUMBUFLEN, "%d", blob_get(b, i));
            ga_concat(gap, numbuf);
         }
         ga_append(gap, ']');
      }
      break;

   case VAR_LIST:
      l = val->list;
      if (!l)
         ga_concat(gap, (CS)"[]");
      else {
         if (l->copyId == copyID)
             ga_concat(gap, (CS)"[]");
         else {
            ListItem   *li;

            l->copyId = copyID;
            ga_append(gap, '[');
            CHECK_LIST_MATERIALIZE(l);
            for (li = l->first; li != NULL && !gotInterruptG; ) {
               if (json_encode_item(gap, &li->c, copyID, 0) == FAIL)
                  return FAIL;
               li = li->next;
               if (li != NULL)
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
            int      first = TRUE;
            int      todo = (int)d->hashTable.count;
            EeSetItem   *hi;

            d->copyId = copyID;
            ga_append(gap, '{');

            for (hi = d->hashTable.array; todo > 0 && !gotInterruptG; ++hi) {
               if (!HASHITEM_EMPTY(hi)) {
                   --todo;
                   if (first)
                  first = FALSE;
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
   int      len;
   Unt      c;
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
               if (res != NULL)
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
               Byte   buf[NUMBUFLEN];

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
            if (res != NULL)
               ga_append(&ga, c);
         }
      } else {
         len = utf_ptr2len(p);
         if (res) {
            if (ga_grow(&ga, len) == FAIL) {
               ga_clear(&ga);
               return FAIL;
            }
            mch_memmove((Byte *)ga.c + ga.len, p, (Unt)len);
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
   Byte   *p;
   int      i;
   int      len;
   int      retval;
   ArrayList   stack;
   Var   item;
   Var   *cur_item;
   JsonDecodeItem* topJson;
   Byte   key_buf[NUMBUFLEN];

   ga_init2(&stack, sizeof(JsonDecodeItem), 100);
   cur_item = res;
   initVarToNull(OUT &item);
   if (res)
      initVarToNull(OUT res);

   fill_numbuflen(reader);
   p = reader->js_buf + reader->js_used;
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
            Byte  *sp = p;

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

                  len = string2float(p, &f, FALSE);
               } else {
                  cur_item->tag = VAR_FLOAT;
                  len = string2float(p, &cur_item->floatt, FALSE);
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
f_json_decode(Var *argvars, Var *returnVar) {
   JsReader   reader;
   reader.js_buf = tv_get_string(&argvars[0]);
   reader.js_fill = NULL;
   reader.js_used = 0;
   json_decode_all(OUT returnVar, &reader);
}

void
f_json_encode(Var *argvars, Var *returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = json_encode(&argvars[0], 0);
}
//}}}
//{{{floating-point numerics

#define USING_FLOAT_STUFF

// Convert the string "text" to a floating point number.
// This uses strtod().  setlocale(LC_NUMERIC, "C") has been used to make sure
// this always uses a decimal point.
// Return the length of the text that was consumed.
int
string2float(
   CS text,
   double   *value,       // result stored here
   int      skip_quotes)
{
   CS s = text;
   double   f;

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
      Byte       buf[100];
      Byte       *p;
      int       quotes = 0;

      copySubstrToAllocation(buf, (Text){s, 99});
      for (p = buf; ; p = skipdigits(p)) {
         // remove single quotes between digits, not in the exponent
         if (*p == '\'') {
            ++quotes;
            mch_memmove(p, p + 1, STRLEN(p));
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
get_float_arg(Var *argvars, double *f) {
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
f_abs(Var *argvars, Var *returnVar) {
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
f_acos(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = acos(f);
   else
      returnVar->floatt = 0.0;
}

void
f_asin(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = asin(f);
   else
      returnVar->floatt = 0.0;
}

void
f_atan(Var *argvars, Var *returnVar) {
   double   f = 0.0;
   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = atan(f);
   else
      returnVar->floatt = 0.0;
}

void
f_atan2(Var *argvars, Var *returnVar) {
   double   fx = 0.0, fy = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &fx) == OK
                 && get_float_arg(&argvars[1], &fy) == OK)
      returnVar->floatt = atan2(fx, fy);
   else
      returnVar->floatt = 0.0;
}

// "ceil({float})" function
void
f_ceil(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = ceil(f);
   else
      returnVar->floatt = 0.0;
}

void
f_cos(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = cos(f);
   else
      returnVar->floatt = 0.0;
}

void
f_cosh(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = cosh(f);
    else
   returnVar->floatt = 0.0;
}

void
f_exp(Var *argvars, Var *returnVar) {
    double   f = 0.0;
   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = exp(f);
   else
      returnVar->floatt = 0.0;
}

// "float2nr({float})" function
void
f_float2nr(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   if (get_float_arg(argvars, &f) != OK)
      return;

   if (f <= (double)-VARNUM_MAX + DBL_EPSILON)
      returnVar->number = -VARNUM_MAX;
   ei (f >= (double)VARNUM_MAX - DBL_EPSILON)
      returnVar->number = VARNUM_MAX;
   else
      returnVar->number = (Long)f;
}

// "floor({float})" function
void
f_floor(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = floor(f);
   else
      returnVar->floatt = 0.0;
}

// "fmod()" function
void
f_fmod(Var *argvars, Var *returnVar) {
    double   fx = 0.0, fy = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &fx) == OK
                 && get_float_arg(&argvars[1], &fy) == OK)
      returnVar->floatt = fmod(fx, fy);
   else
      returnVar->floatt = 0.0;
}

// "isinf()" function
void
f_isinf(Var *argvars, Var *returnVar) {
   if (argvars[0].tag == VAR_FLOAT && isinf(argvars[0].floatt))
      returnVar->number = argvars[0].floatt > 0.0 ? 1 : -1;
}

// "isnan()" function
void
f_isnan(Var *argvars, Var *returnVar) {
   returnVar->number = argvars[0].tag == VAR_FLOAT && isnan(argvars[0].floatt);
}

/*
 * "log()" function
 */
void
f_log(Var *argvars, Var *returnVar) {
    double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = log(f);
   else
      returnVar->floatt = 0.0;
}

void
f_log10(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = log10(f);
   else
      returnVar->floatt = 0.0;
}

void
f_pow(Var *argvars, Var *returnVar) {
    double   fx = 0.0, fy = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &fx) == OK
                 && get_float_arg(&argvars[1], &fy) == OK)
      returnVar->floatt = pow(fx, fy);
   else
      returnVar->floatt = 0.0;
}


// round() is not in C90, use ceil() or floor() instead.
private double
eeRound(double f) {
   return f > 0 ? floor(f + 0.5) : ceil(f - 0.5);
}

void
f_sqrt(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = sqrt(f);
   else
      returnVar->floatt = 0.0;
}

void
f_str2float(Var *argvars, Var *returnVar) {
   Byte *p;
   int     isneg;
   int       skip_quotes;
   skip_quotes = argvars[1].tag != VAR_UNKNOWN && tv_get_bool(&argvars[1]);

   p = skipwhite(tv_get_string_strict(&argvars[0]));
   isneg = (*p == '-');

   if (*p == '+' || *p == '-')
      p = skipwhite(p + 1);
   (void)string2float(p, &returnVar->floatt, skip_quotes);
   if (isneg)
      returnVar->floatt *= -1;
   returnVar->tag = VAR_FLOAT;
}

void
f_tan(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = tan(f);
   else
      returnVar->floatt = 0.0;
}

// "tanh()" function
void
f_tanh(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = tanh(f);
   else
      returnVar->floatt = 0.0;
}

// "trunc({float})" function
void
f_trunc(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      // trunc() is not in C90, use floor() or ceil() instead.
      returnVar->floatt = f > 0 ? floor(f) : ceil(f);
   else
      returnVar->floatt = 0.0;
}

// "round({float})" function
void
f_round(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = eeRound(f);
   else
      returnVar->floatt = 0.0;
}

void
f_sin(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
      returnVar->floatt = sin(f);
   else
      returnVar->floatt = 0.0;
}

void
f_sinh(Var *argvars, Var *returnVar) {
   double   f = 0.0;

   returnVar->tag = VAR_FLOAT;
   if (get_float_arg(argvars, &f) == OK)
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
    Byte  *sname = estack_sfile(ESTACK_NONE);

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
ga_concat_esc(ArrayList *gap, Byte *p, int clen) {
   Byte  buf[NUMBUFLEN];

   if (clen > 1) {
      mch_memmove(buf, p, clen);
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
         }
         else
             ga_append(gap, *p);
         break;
    }
}

//Append "str" to "gap", escaping unprintable characters.
//Changes NL to \n, CR to \r, etc.
private void
ga_concat_shorten_esc(ArrayList *gap, Byte *str) {
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
   Byte      *exp_str,
   Var   *exp_tv_arg,
   Var   *got_tv_arg,
   AssertKind assKind
) {
   Byte   numbuf[NUMBUFLEN];
   Byte   *tofree;
   Var   *exp_tv = exp_tv_arg;
   Var   *got_tv = got_tv_arg;
   int      did_copy = FALSE;
   int      omitted = 0;

   if (opt_msg_tv->tag != VAR_UNKNOWN
       && !(opt_msg_tv->tag == VAR_STRING
      && (opt_msg_tv->string == NULL
          || *opt_msg_tv->string == ZERO)))
    {
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

      did_copy = TRUE;
      exp_tv->bag = allocBag();
      got_tv->bag = allocBag();

      todo = (int)exp_d->hashTable.count;
      FOR_ALL_HASHTAB_ITEMS(&exp_d->hashTable, hi, todo) {
         if (!HASHITEM_EMPTY(hi)) {
            item2 = bagFind(got_d, textOfItem(hi));
            if (!item2 || !tv_equal(&HI2DI(hi)->c, &item2->c, FALSE)) {
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
assert_equal_common(Var *argvars, AssertKind assKind) {
   if (tv_equal(&argvars[0], &argvars[1], FALSE) != (assKind == ASSERT_EQUAL)) {
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
assert_match_common(Var *argvars, AssertKind assKind) {
   ArrayList   ga;
   Byte   buf1[NUMBUFLEN];
   Byte   buf2[NUMBUFLEN];
   CS pat = convertVarToString(&argvars[0], buf1);
   CS text = convertVarToString(&argvars[1], buf2);
   if (pat && text && pattern_match(pat, text, FALSE) != (assKind == ASSERT_MATCH)) {
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
assert_bool(Var *argvars, int isTrue) {
   Boole error = false;
   ArrayList   ga;

   if (argvars[0].tag == VAR_BOOL && argvars[0].number == (isTrue ? VVAL_TRUE : VVAL_FALSE))
      return 0;
   if (argvars[0].tag != VAR_NUMBER
       || (varGetNumberChk(argvars, OUT &error) == 0) == isTrue
       || error)
    {
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
assert_append_cmd_or_arg(ArrayList *gap, Var *argvars, Byte *cmd) {
   Byte   *tofree;
   Byte   numbuf[NUMBUFLEN];

   if (argvars[1].tag != VAR_UNKNOWN && argvars[2].tag != VAR_UNKNOWN) {
      ga_concat(gap, echo_string(&argvars[2], &tofree, numbuf, 0));
      eeglFree(tofree);
   } else
      ga_concat(gap, cmd);
}

//"assert_equal(expected, actual[, msg])" function
void
f_assert_equal(Var *argvars, Var *returnVar) {
   returnVar->number = assert_equal_common(argvars, ASSERT_EQUAL);
}

private int
assert_equalfile(Var *argvars) {
   Byte   buf1[NUMBUFLEN];
   Byte   buf2[NUMBUFLEN];
   Byte   *fname1 = convertVarToString(&argvars[0], buf1);
   Byte   *fname2 = convertVarToString(&argvars[1], buf2);

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
               eeSnprintf(IObuff, IOSIZE,
                         "difference at byte %ld, line %ld", count, linecount);
               break;
                }
            }
            ++count;
            if (c1 == NL) {
                ++linecount;
                lineidx = 0;
            } ei (lineidx + 2 == (int)sizeof(line1)) {
                mch_memmove(line1, line1 + 100, lineidx - 100);
                mch_memmove(line2, line2 + 100, lineidx - 100);
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
       Byte   numbuf[NUMBUFLEN];
       Byte   *tofree;

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
f_assert_equalfile(Var *argvars, Var *returnVar) {
   returnVar->number = assert_equalfile(argvars);
}

//"assert_notequal(expected, actual[, msg])" function
void
f_assert_notequal(Var *argvars, Var *returnVar) {
   returnVar->number = assert_equal_common(argvars, ASSERT_NOTEQUAL);
}

//"assert_exception(string[, msg])" function
void
f_assert_exception(Var *argvars, Var *returnVar) {
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
f_assert_fails(Var *argvars, Var *returnVar) {
   ArrayList   ga;
   int      save_trylevel = trylevel;
   int      called_emsg_before = called_emsg;
   CS wrong_arg_msg = NULL;
   Byte   *tofree = NULL;

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
   suppress_errthrow = TRUE;
   in_assert_fails = TRUE;
   ++no_wait_return;

   Byte *cmd = convertVarToStringSingleUse(&argvars[0]);
   executeCommLine(cmd);

   // reset here for any errors reported below
   trylevel = save_trylevel;
   suppress_errthrow = FALSE;

   if (called_emsg == called_emsg_before) {
      prepare_assert_error(&ga);
      ga_concat(&ga, (CS)"command did not fail: ");
      assert_append_cmd_or_arg(&ga, argvars, cmd);
      assert_error(&ga);
      ga_clear(&ga);
      returnVar->number = 1;
   } ei (argvars[1].tag != VAR_UNKNOWN) {
      Byte   buf[NUMBUFLEN];
      Byte   *expected;
      Byte   *expected_str = NULL;
      int   error_found = FALSE;
      int   error_found_index = 1;
      Byte   *actual = emsg_assert_fails_msg == NULL ? (CS)"[unknown]"
                            : emsg_assert_fails_msg;

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
         if (!pattern_match(expected, actual, FALSE)) {
            error_found = TRUE;
            expected_str = expected;
         } ei (list->len == 2) {
            // make a copy, an error in pattern_match() may free it
            tofree = actual = copyStr(get_EeglVar_str(VV_ERRMSG));
            tv = &list->lv_u.mat.last->c;
            expected = convertVarToString(tv, buf);
            if (expected == NULL)
               goto theend;
            if (!pattern_match(expected, actual, FALSE)) {
               error_found = TRUE;
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
            error_found = TRUE;
            error_found_index = 3;
          }
         if (!error_found && argvars[4].tag != VAR_UNKNOWN) {
            if (argvars[4].tag != VAR_STRING) {
                wrong_arg_msg = e_assert_fails_fifth_argument;
                goto theend;
            } ei (argvars[4].string 
                  && !pattern_match(argvars[4].string, emsg_assert_fails_context, FALSE)
            ) {
               error_found = TRUE;
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
   suppress_errthrow = FALSE;
   in_assert_fails = FALSE;
   anyEmsgG = FALSE;
   gotInterruptG = FALSE;
   msgColG = 0;
   --no_wait_return;
   need_wait_return = FALSE;
   emsg_on_display = FALSE;
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
f_assert_false(Var *argvars, Var *returnVar) {
   returnVar->number = assert_bool(argvars, FALSE);
}

private int
assert_inrange(Var *argvars) {
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
      Long   lower = varGetNumberChk(&argvars[0], OUT &error);
      Long   upper = varGetNumberChk(&argvars[1], OUT &error);
      Long   actual = varGetNumberChk(&argvars[2], OUT &error);

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
f_assert_inrange(Var *argvars, Var *returnVar) {
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
f_assert_match(Var *argvars, Var *returnVar) {
   returnVar->number = assert_match_common(argvars, ASSERT_MATCH);
}

//"assert_notmatch(pattern, actual[, msg])" function
void
f_assert_notmatch(Var *argvars, Var *returnVar) {
   returnVar->number = assert_match_common(argvars, ASSERT_NOTMATCH);
}

//"assert_report(msg)" function
void
f_assert_report(Var *argvars, Var *returnVar) {
   ArrayList   ga;
   prepare_assert_error(OUT &ga);
   ga_concat(&ga, tv_get_string(&argvars[0]));
   assert_error(&ga);
   ga_clear(&ga);
   returnVar->number = 1;
}

//"assert_true(actual[, msg])" function
void
f_assert_true(Var *argvars, Var *returnVar) {
   returnVar->number = assert_bool(argvars, TRUE);
}

//"test_alloc_fail(id, countdown, repeat)" function
void
f_test_alloc_fail(Var *argvars, Var *returnVar UNUSED) {
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
      did_outofmem_msg = FALSE;
   }
}

void
f_test_autochdir(Var *argvars UNUSED, Var *returnVar UNUSED) {
}

void
f_test_feedinput(Var *argvars, Var *returnVar UNUSED) {
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
f_test_getvalue(Var *argvars, Var *returnVar) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   CS  name = tv_get_string(&argvars[0]);

   if (STRCMP(name, (CS)"need_fileinfo") == 0)
      returnVar->number = need_fileinfo;
   else
      showErrFmtMsg(_(e_invalid_argument_str), name);
}

//"test_option_not_set({name})" function
void
f_test_option_not_set(Var *argvars, Var *returnVar UNUSED) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   CS name = tv_get_string(&argvars[0]);
   if (reset_optWasSet(name) == FAIL)
      showErrFmtMsg(_(e_invalid_argument_str), name);
}

//"test_override({name}, {val})" function
void
f_test_override(Var *argvars, Var *returnVar UNUSED) {
   Byte *name = Em;
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
      disable_char_avail_for_testing = FALSE;
      disable_redraw_for_testing = FALSE;
      ignore_redraw_flag_for_testing = FALSE;
      nfa_fail_for_testing = FALSE;
      no_query_mouse_for_testing = FALSE;
      ui_delay_for_testing = 0;
      reset_term_props_on_termresponse = FALSE;
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
f_test_refcount(Var *argvars, Var *returnVar) {
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

         fp = find_func(argvars[0].string, FALSE);
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
f_test_garbagecollect_now(Var *argvars UNUSED, Var *returnVar UNUSED) {
    // This is dangerous, any Lists and Dicts used internally may be freed while still in use.
    if (!get_EeglVar_nr(VV_TESTING))
   emsg(_(e_calling_test_garbagecollect_now_while_v_testing_is_not_set));
    else
   garbage_collect(TRUE);
}

void
f_test_garbagecollect_soon(Var *argvars UNUSED, Var *returnVar UNUSED) {
   may_garbage_collect = TRUE;
}

void
f_test_ignore_error(Var *argvars, Var *returnVar UNUSED) {
   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   ignore_error_for_testing(tv_get_string(&argvars[0]));
}

void
f_test_null_blob(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_BLOB;
   returnVar->blob = NULL;
}

void
f_test_null_channel(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_CHANNEL;
   returnVar->channel = NULL;
}

void
f_test_null_dict(Var *argvars UNUSED, Var *returnVar) {
   returnVar_dict_set(returnVar, NULL);
}

void
f_test_null_job(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_JOB;
   returnVar->job = NULL;
}

void
f_test_null_list(Var *argvars UNUSED, Var *returnVar) {
   returnVar_list_set(returnVar, NULL);
}

void
f_test_null_function(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_FUNC;
   returnVar->string = NULL;
}

void
f_test_null_partial(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_PARTIAL;
   returnVar->partial = NULL;
}

void
f_test_null_string(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
}

void
f_test_unknown(Var *argvars UNUSED, Var *returnVar) {
    returnVar->tag = VAR_UNKNOWN;
}

void
f_test_void(Var *argvars UNUSED, Var *returnVar) {
    returnVar->tag = VAR_VOID;
}

void
f_test_setmouse(Var *argvars, Var *returnVar UNUSED) {
   if (argvars[0].tag != VAR_NUMBER || argvars[1].tag != VAR_NUMBER) {
      emsg(_(e_invalid_argument));
      return;
   }

   mouseRowG = (time_t)tv_get_number(&argvars[0]) - 1;
   mouseColG = (time_t)tv_get_number(&argvars[1]) - 1;
}

void
f_test_settime(Var *argvars, Var *returnVar UNUSED) {
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
// "exclusive" is TRUE for slice(): second index is exclusive, use character index for string.
// Alternatively, "key" is not NULL, then key[keylen] is the dict index.
int
eval_index_inner(
   Var    *returnVar,
   int       is_range,
   Var    *var1,
   Var    *var2,
   int       exclusive,
   Byte       *key,
   int       keylen,
   int       verbose)
{
   Long       n1, n2 = 0;
   long       len;

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
         n2 = VARNUM_MAX;
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
      Byte   *s = tv_get_string(returnVar);

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
         n2 = VARNUM_MAX;
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
//{{{searchin' an' sortin'

// Return index of key in a sorted array, or -1 if not found.
int
binarySearch_Unt(Unt key, int start, int end, Arr(Unt) arr) {
   if (end <= start) {
      return -1;
   }
   int i = start;
   int j = end - 1;
   if (arr[start] == key) {
      return i;
   } ei (arr[j] == key) {
      return j;
   }

   while (i < j) {
      if (j - i == 1) {
         return -1;
      }
      int midInd = (i + j)/2;
      Unt mid = arr[midInd];
      if (mid > key) {
         j = midInd;
      } ei (mid < key) {
         i = midInd;
      } else {
         return midInd;
      }
   }
   return -1;
}

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
returnVar_blob_alloc(Var *returnVar) {
   Blob   *b = blob_alloc();

   if (!b)
      return FAIL;

   returnVar_blob_set(returnVar, b);
   return OK;
}

// Set a blob as the return value.
void
returnVar_blob_set(Var *returnVar, Blob *b) {
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

// Return TRUE when two blobs have exactly the same values.
int
blob_equal(Blob   *b1, Blob   *b2) {
   int       i;
   int       len1 = blob_len(b1);
   int       len2 = blob_len(b2);

   // empty and NULL are considered the same
   if (len1 == 0 && len2 == 0)
      return TRUE;
   if (b1 == b2)
      return TRUE;
   if (len1 != len2)
      return FALSE;
   for (i = 0; i < b1->c.len; i++)
      if (blob_get(b1, i) != blob_get(b2, i)) return FALSE;
   return TRUE;
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
      
   Byte  *s = str;
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
blob_add(Var *argvars, Var *returnVar) {
   Blob   *b = argvars[0].blob;
   Boole error = false;

   if (!b) {
      return;
   }

   if (value_check_lock(b->lock, text(N_("add() argument")), TRUE))
      return;

   Long n = varGetNumberChk(&argvars[1], &error);
   if (error)
      return;

   ga_append(&b->c, (int)n);
   copy_tv(OUT returnVar, &argvars[0]);
}

// "remove({blob}, {idx} [, {end}])" function
void
blob_remove(Var *argvars, Var *returnVar, Byte *arg_errmsg) {
   Blob   *b = argvars[0].blob;
   Blob   *newblob;
   Boole error = false;
   long   idx;
   long   end;
   int      len;
   Byte   *p;

   if (b && value_check_lock(b->lock, mbText(arg_errmsg), TRUE))
      return;

   idx = (long)varGetNumberChk(&argvars[1], &error);
   if (error)
      return;

   len = blob_len(b);

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
      mch_memmove(p + idx, p + idx + 1, (Unt)len - idx - 1);
      --b->c.len;
      return;
   }

   // Remove range of items, return blob with values.
   end = (long)varGetNumberChk(&argvars[2], &error);
   if (error)
      return;
   if (end < 0)
   // count from the end
   end = len + end;
   if (end >= len || idx > end) {
      showErrFmtMsg(_(e_blob_index_out_of_range_nr), end);
      return;
   }
   newblob = blob_alloc();
   if (newblob == NULL)
      return;
   newblob->c.len = end - idx + 1;
   if (ga_grow(&newblob->c, end - idx + 1) == FAIL) {
      eeglFree(newblob);
      return;
   }
   p = (CS)b->c.c;
   mch_memmove((CS)newblob->c.c, p + idx, (Unt)(end - idx + 1));
   ++newblob->refcount;
   returnVar->tag = VAR_BLOB;
   returnVar->blob = newblob;

   if (len - end - 1 > 0)
      mch_memmove(p + idx, p + end + 1, (Unt)(len - end - 1));
   b->c.len -= end - idx + 1;
}

//Implementation of map() and filter() for a Blob.  Apply "expr" to every
//number in Blob "blob_arg" and return the result in "returnVar".
void
blob_filter_map(
   Blob      *blob_arg,
   FilterMap   filtermap,
   Var   *expr,
   Byte      *arg_errmsg,
   Var   *returnVar)
{
   Blob   *b = blob_arg;
   int      i;
   Var   tv;
   Long   val;
   Blob   *b_ret;
   int      idx = 0;
   int      rem;
   Var   newtv;

   if (filtermap == FILTERMAP_MAPNEW) {
      returnVar->tag = VAR_BLOB;
      returnVar->blob = NULL;
   }
   if (!b || (filtermap == FILTERMAP_FILTER && value_check_lock(b->lock, mbText(arg_errmsg), TRUE)))
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
            Byte *p = (CS)blob_arg->c.c;
            mch_memmove(p + i, p + i + 1, (Unt)b->c.len - i - 1);
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
blob_insert_func(Var *argvars, Var *returnVar) {
   Blob   *b = argvars[0].blob;
   long   before = 0;
   Boole error = false;
   int      val, len;
   Byte   *p;

   if (!b) {
      return;
   }

   if (value_check_lock(b->lock, text(N_("insert() argument")), TRUE))
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
   mch_memmove(p + before + 1, p + before, (Unt)len - before);
   *(p + before) = val;
   ++b->c.len;

   copy_tv(OUT returnVar, &argvars[0]);
}

// Implementation of reduce() for Blob "argvars[0]" using the function "expr"
// starting with the optional initial value "argvars[2]" and return the result in "returnVar".
void
blob_reduce(
   Var   *argvars,
   Var   *expr,
   Var   *returnVar)
{
   Blob   *b = argvars[0].blob;
   int      called_emsg_start = called_emsg;
   int      r;
   Var   initial;
   Var   argv[3];
   int   i;

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
   if (b == NULL)
      return;

   for ( ; i < b->c.len; i++) {
   argv[0] = *returnVar;
   argv[1].tag = VAR_NUMBER;
   argv[1].number = blob_get(b, i);

   r = eval_expr_typval(expr, TRUE, argv, 2, returnVar);

   clearVar(&argv[0]);
   if (r == FAIL || called_emsg != called_emsg_start)
       return;
   }
}

void
blob_reverse(Blob *b, Var *returnVar) {
   int   i, len = blob_len(b);

   for (i = 0; i < len / 2; i++) {
      int tmp = blob_get(b, i);

      blob_set(b, i, blob_get(b, len - i - 1));
      blob_set(b, len - i - 1, tmp);
   }
   returnVar_blob_set(returnVar, b);
}

void
f_blob2list(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   if (check_for_blob_arg(argvars, 0) == FAIL)
      return;

   Blob* blob = argvars->blob;
   List* l = returnVar->list;
   for (int i = 0; i < blob_len(blob); i++)
      list_append_number(l, blob_get(blob, i));
}

void
f_list2blob(Var *argvars, Var *returnVar) {
   ListItem   *li;

   if (returnVar_blob_alloc(returnVar) == FAIL)
      return;
   Blob* blob = returnVar->blob;

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   List* l = argvars->list;
   if (l == NULL)
      return;

   CHECK_LIST_MATERIALIZE(l);
   FOR_ALL_LIST_ITEMS(l, li) {
      Boole error = false;
      Long n = varGetNumberChk(&li->c, &error);
      if (error == TRUE || n < 0 || n > 255) {
         if (!error)
            showErrFmtMsg(_(e_invalid_value_for_blob_nr), n);
         ga_clear(&blob->c);
         return;
      }
      ga_append(&blob->c, n);
   }
}

//}}}
