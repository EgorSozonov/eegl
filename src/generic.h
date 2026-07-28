//{{{ List
//{{{ add

#ifdef ADD_LIST_TY
#ifndef ADD_LIST_ONCE

#define ADD_LIST_OVERLOAD(n, T)\
typedef _GL(L, T*) _GL(L_add_, n);\
private void add_##n (T newItem, _GL(L, T) * st) {\
   if (st->len < st->cap) {\
      st->c[st->len] = newItem;\
   } else {\
      T* newCont = allocateArray(2*(st->cap), T, st->a);\
      memcpy(newCont, st->c, st->len*sizeof(T));\
      newCont[st->len] = newItem;\
      st->c = newCont;\
      st->cap *= 2;\
   }\
   st->len++;\
}

#define add(a, b) _Generic((b),        \
   IF_DEF(ADD_LIST_0)( L_add_0: add_0) \
   IF_DEF(ADD_LIST_1)(,L_add_1: add_1) \
   IF_DEF(ADD_LIST_2)(,L_add_2: add_2) \
   IF_DEF(ADD_LIST_3)(,L_add_3: add_3) \
   IF_DEF(ADD_LIST_4)(,L_add_4: add_4) \
   IF_DEF(ADD_LIST_5)(,L_add_5: add_5) \
   IF_DEF(ADD_LIST_6)(,L_add_6: add_6) \
   IF_DEF(ADD_LIST_7)(,L_add_7: add_7) \
)((a), b)

#define ADD_LIST_ONCE
#endif

//Insert a function overload into the first available slot.
#if     !defined( ADD_LIST_0 )
#define ADD_LIST_0          
ADD_LIST_OVERLOAD(0, ADD_LIST_TY)
#elif   !defined( ADD_LIST_1 )
#define ADD_LIST_1
ADD_LIST_OVERLOAD(1, ADD_LIST_TY)
#elif   !defined( ADD_LIST_2 )
#define ADD_LIST_2
ADD_LIST_OVERLOAD(2, ADD_LIST_TY)
#elif   !defined( ADD_LIST_3 )
#define ADD_LIST_3
ADD_LIST_OVERLOAD(3, ADD_LIST_TY)
#elif   !defined( ADD_LIST_4 )
#define ADD_LIST_4
ADD_LIST_OVERLOAD(4, ADD_LIST_TY)
#elif   !defined( ADD_LIST_5 )
#define ADD_LIST_5
ADD_LIST_OVERLOAD(5, ADD_LIST_TY)
#elif   !defined(ADD_LIST_6)
#define ADD_LIST_6
ADD_LIST_OVERLOAD(6, ADD_LIST_TY)
#elif   !defined(ADD_LIST_7)
#define ADD_LIST_7
ADD_LIST_OVERLOAD(7, ADD_LIST_TY)
#else
#error Sorry, too many "add to list" function overloads!
#endif

//Undef so that the user doesn't have to.
#undef ADD_LIST_TY
#endif

//}}}
//{{{ removeLast

#ifdef REMOVE_LAST_LIST_TY
#ifndef REMOVE_LAST_LIST_ONCE

#define REMOVE_LAST_LIST_OVERLOAD(n, T)\
typedef _GL(L, T*) _GL(L_removeLast_, n);\
private T removeLast_##n (_GL(L_, T) * l) {\
      l->len--;\
      return l->c[l->len];\
}

#define removeLast(b) _Generic((b),\
  IF_DEF( REMOVE_LAST_LIST_0 )( L_removeLast_0: removeLast_0) \
  IF_DEF( REMOVE_LAST_LIST_1 )(,L_removeLast_1: removeLast_1) \
  IF_DEF( REMOVE_LAST_LIST_2 )(,L_removeLast_2: removeLast_2) \
  IF_DEF( REMOVE_LAST_LIST_3 )(,L_removeLast_3: removeLast_3) \
  IF_DEF( REMOVE_LAST_LIST_4 )(,L_removeLast_4: removeLast_4) \
  IF_DEF( REMOVE_LAST_LIST_5 )(,L_removeLast_5: removeLast_5) \
  IF_DEF( REMOVE_LAST_LIST_6 )(,L_removeLast_6: removeLast_6) \
  IF_DEF( REMOVE_LAST_LIST_7 )(,L_removeLast_7: removeLast_7) \
)((b))

#define REMOVE_LAST_LIST_ONCE

#endif


// Insert a function overload into the first available slot.
#if     !defined( REMOVE_LAST_LIST_0 )
#define REMOVE_LAST_LIST_0          
REMOVE_LAST_LIST_OVERLOAD(0, REMOVE_LAST_LIST_TY)
#elif   !defined( REMOVE_LAST_LIST_1 )
#define REMOVE_LAST_LIST_1
REMOVE_LAST_LIST_OVERLOAD(1, REMOVE_LAST_LIST_TY)
#elif   !defined( REMOVE_LAST_LIST_2 )
#define REMOVE_LAST_LIST_2
REMOVE_LAST_LIST_OVERLOAD(2, REMOVE_LAST_LIST_TY)
#elif   !defined( REMOVE_LAST_LIST_3 )
#define REMOVE_LAST_LIST_3
REMOVE_LAST_LIST_OVERLOAD(3, REMOVE_LAST_LIST_TY)
#elif   !defined( REMOVE_LAST_LIST_4 )
#define REMOVE_LAST_LIST_4
REMOVE_LAST_LIST_OVERLOAD(4, REMOVE_LAST_LIST_TY)
#elif   !defined( REMOVE_LAST_LIST_5 )
#define REMOVE_LAST_LIST_5
REMOVE_LAST_LIST_OVERLOAD(5, REMOVE_LAST_LIST_TY)
#elif   !defined(REMOVE_LAST_LIST_6)
#define REMOVE_LAST_LIST_6
REMOVE_LAST_LIST_OVERLOAD(6, REMOVE_LAST_LIST_TY)
#elif   !defined(REMOVE_LAST_LIST_7)
#define REMOVE_LAST_LIST_7
REMOVE_LAST_LIST_OVERLOAD(7, REMOVE_LAST_LIST_TY)
#else
#error Sorry, too many "removeLast of list" function overloads!
#endif

// Undef so that the user doesn't have to.
#undef REMOVE_LAST_LIST_TY
#endif

//}}}
//}}}
