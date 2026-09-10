#define GEN_TYPE_L(T) typedef struct {\
   T* c;\
   Unt len;\
   Unt cap;\
   Arena* a;\
} L##T;;
#define GEN_add_L(acc, T) p##acc void add_L##T (L##T * l, T newItem) {\
   if (l->len < l->cap) {\
      l->c[l->len] = newItem;\
   } else {\
      T* newCont = allocateArray(2*(l->cap), T, l->a);\
      memcpy(newCont, l->c, l->len*sizeof(T));\
      newCont[l->len] = newItem;\
      l->c = newCont;\
      l->cap *= 2;\
   }\
   l->len++;\
};
#define GEN_create_L(acc, T)\
p##acc L##T * create_L##T (int initCapacity, Arena* a) {\
   int capacity = initCapacity < 4 ? 4 : initCapacity;\
   L##T * result = allocate(L##T, a);\
   result->cap = capacity;\
   result->len = 0;\
   result->a = a;\
   T* arr = allocateArray(capacity, T, a);\
   result->c = arr;\
   return result;\
};
#define last(l) (l)->c[(l)->len - 1];
#define sLast(l) (l).c[(l).len - 1];
#define eq(a, b) _Generic((a),\
   Text: _Generic((b),\
         Text: eq_Text_Text,\
         CS: eq_Text_CString\
      ),\
   CS: eq_CString_CString\
)(a, b);
