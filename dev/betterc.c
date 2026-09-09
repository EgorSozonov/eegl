#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

//{{{base

#define OUT
#define NULLABLE

#define pub
#define private static //full private (affects linking) - for functions
#define comptime //compile-time private (doesn't affect linking) - for types and macros

typedef unsigned char Byte;

#define _pure __attribute__((pure))

#define ZERO 0
#define false 0
#define true 1
#define ei else if
#define Arr(T) T*
#define null nullptr
#define UNT 4294967295       //2**32 - 1
#define LOWER24BITS 0x00FFFFFF

typedef char* S;
typedef char Boole;
typedef uint32_t Unt;
typedef int32_t Int;

#define generic(...)

comptime typedef struct { //:Text
   S c;
   Unt len;
} Text;

comptime typedef struct { //:Slice
   Unt c; //offset within the file
   Unt len;
} Slice; //slice of text referencing the current source code file

private _pure Text
text(S s) {
   return (Text){s, strlen(s)};
}

private _pure Text
textOfSlice(Slice sl, S container) {
   return (Text){container + sl.c, sl.len};
}

__attribute__((error("Expression is not a constant"))) Text
notconst(void);

#define tConst(s) __builtin_constant_p(s) ? text(s) : notconst()

pub typedef struct {
   S msg;
} Error;

comptime typedef struct {
   S c;       //absolute filename
   Unt dirLen; //length including the last '/' in the filename
   Unt len;    //full length (so that the short name lies in [dirLen + 1; len)
} FilePath;

private FilePath
filePath(S rawFname) {
   if (!rawFname) {
      return (FilePath){};
   }
   Unt dirLen = 0;
   Unt len = strlen(rawFname);
   for (Unt i = len - 1; i < len; i--) {
      if (rawFname[i] == '/') {
         dirLen = i + 1;
         break;
      }
   }
   return (FilePath){rawFname, dirLen, len};
}

//}}}
//{{{arena

typedef struct Arena Arena;
Arena* createArena();
void deleteArena(Arena* ar);
void* allocateOnArena(Unt, Arena*);
#define allocate(T, a) (T*)allocateOnArena(sizeof(T), a)
#define allocateArray(cap, T, a) (T*)allocateOnArena(cap*sizeof(T), a)

#define CHUNK_QUANT 32768

comptime typedef struct ArenaChunk ArenaChunk;

comptime struct ArenaChunk { // :ArenaChunk
   Unt size;
   ArenaChunk* next;
   char memory[]; // flexible array member
};

comptime struct Arena { // :Arena
   ArenaChunk* firstChunk;
   ArenaChunk* currChunk;
   int currInd;
};

pub Arena*
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

pub void*
allocateOnArena(Unt allocSize, Arena* a) { //:allocateOnArena
// Allocate memory in the arena, malloc'ing a new chunk if needed
   if ((Unt)a->currInd + allocSize >= a->currChunk->size) {
      if (a->currChunk->next != null && a->currChunk->next->size < allocSize) {
         // the next chunk is big enough, so we skip the rest of this chunk and move on
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

pub void
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

pub void
arenaTryFree(void* start, Unt len, Arena* a) {
// If this memory span is at the very end of this arena, then free it by rewinding
   if ((void*)&(a->currChunk->memory) + (a->currInd - len) == start) {
      a->currInd -= len;
   }
}

//}}}
//{{{util types
//{{{list

#define GEN_TYPE_L(acc, T) acc typedef struct {\
      T* c;\
      Unt len;\
      Unt cap;\
      Arena* a;\
   } L##T;

#define GEN_create_L(acc, T) acc L##T * create_##L##T(int initCapacity, Arena* a) {\
      int capacity = initCapacity < 4 ? 4 : initCapacity;\
      L##T* result = allocate(L##T, a);\
      result->cap = capacity;\
      result->len = 0;\
      result->a = a;\
      T* arr = allocateArray(capacity, T, a);\
      result->c = arr;\
      return result;\
   }

#define GEN_add_L(acc, T) acc void add_L##T (L##T * st, T newItem) {\
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

GEN_TYPE_L(comptime, Text);
//GEN_TYPE_L(comptime, LText);


//generic(private, add, L, uint8_t)

#define create(A, B) create_##A##B

#define add(T, X) _Generic((T),\
   LToken*: add_LToken,\
   LText*: add_LText,\
   LGenericMethod*: add_LGenericMethod\
   )(T, X)


generic(2) GEN_create_L(private, Text);
generic(2) GEN_add_L(private, Text);

//}}}
//{{{String Map

#define initBucketSize 8

// Reference to first occurrence of a string identifier within input text
typedef struct { //:StringValue
   Unt hash;
   Slice key;
   Unt val;
} StringValue;

typedef struct { //:Bucket
   Unt capAndLen;
   StringValue c[];
} Bucket;

// Hash map of all words/identifiers encountered in a source module
typedef struct { //:StringMap
   Arr(Bucket*) dict;
   Unt dictSize;
   S c; //The container holding text referenced by slices in "dict"
   Arena* a;
} StringMap;

private StringMap* //:create_StringMap
create_StringMap(Unt initSize, S source, Arena* a) {
   StringMap* result = allocate(StringMap, a);
   Unt realInitSize = (initSize >= initBucketSize && initSize < 2048)
      ? initSize
      : (initSize >= initBucketSize ? 2048 : initBucketSize);
   Arr(Bucket*) dict = allocateArray(realInitSize, Bucket*, a);

   result->a = a;

   Arr(Bucket*) d = dict;

   for (Unt i = 0; i < realInitSize; i++) {
      d[i] = null;
   }
   result->dictSize = realInitSize;
   result->dict = dict;
   result->c = source;

   return result;
}

private Unt
hashCode(Text txt) { //:hashCode
   Unt result = 5381;
   for (Unt i = 0; i < txt.len; i++) {
      result = ((result << 5) + result) + txt.c[i]; // hash*33 + c
   }

   return result;
}

private void
addValueToBucket(Bucket** ptrToBucket, StringValue val, Arena* a) { //:addValueToBucket
   Bucket* p = *ptrToBucket;
   Unt capacity = (p->capAndLen) >> 16;
   Unt lenBucket = (p->capAndLen & 0xFFFF);
   if (lenBucket + 1 < capacity) {
      *(p->c + lenBucket) = val;
      (p->capAndLen)++;
   } else {
      // TODO handle the case when we're overflowing the 16 bits of capacity
      Bucket* newBucket = allocateOnArena(sizeof(Bucket) + 2*capacity*sizeof(StringValue), a);
      memcpy(newBucket->c, p->c, capacity*sizeof(StringValue));

      Arr(StringValue) newValues = (StringValue*)newBucket->c;
      newValues[capacity] = val;
      *ptrToBucket = newBucket;
      newBucket->capAndLen = ((2*capacity) << 16) + capacity + 1;
   }
}

//Add a key-value pair to a string-int hashmap. Does NOT do anything if the key is already present
//(there is "upsert" for that)
private void
put_StringMap(Slice key, Unt val, OUT StringMap* hm) { //:put_StringMap
   Unt hash = hashCode(textOfSlice(key, hm->c));
   Unt dictInd = hash % (hm->dictSize);
   Bucket* bu = *(hm->dict + dictInd);

   if (bu) {
      Unt lenBucket = (bu->capAndLen & 0xFFFF);
      for (Unt i = 0; i < lenBucket; i++) {
         StringValue strVal = bu->c[i];
         if (strVal.hash == hash 
               && strVal.key.len == key.len
               && memcmp(text + (hm->c[strVal.key.c]), text + key.c, key.len) == 0
         ) {
            // key already present
            bu->c[i].val = val;
            return;
         }
      }
      StringValue newVal = (StringValue) {.key = key, .val = val, .hash = hash};
      addValueToBucket(hm->dict + dictInd, newVal, hm->a);
   } else {
      Bucket* newBucket = allocateOnArena(sizeof(Bucket) + initBucketSize*sizeof(StringValue), hm->a);
      newBucket->capAndLen = (initBucketSize << 16) + 1; // left Short = cap, right Short = len
      StringValue* firstElem = (StringValue*)newBucket->c;
      *firstElem = (StringValue){.hash = hash, .key = key, .val = val };
      *(hm->dict + dictInd) = newBucket;
   }
}

private Unt
get_StringMap(Slice needle, StringMap* hm) { //:get_StringMap
// Returns the index of a string within the string table, or UNT if it's absent
   Unt hash = hashCode(textOfSlice(needle, hm->c));
   NULLABLE Bucket* b = hm->dict[hash % (hm->dictSize)];
   if (b) {
      Unt lenBucket = (b->capAndLen & 0xFFFF);
      Arr(StringValue) stringValues = (Arr(StringValue))b->c;
      for (Unt i = 0; i < lenBucket; i++) {
         if (stringValues[i].hash == hash 
            && stringValues[i].key.len == needle.len
            && memcmp(hm->c + needle.c, hm->c + stringValues[i].key.c, needle.len) == 0
         ) {
            return stringValues[i].val;
         }
      }
   }
   return UNT;
}

private Boole
isNonempty_StringMap(StringMap* hm) {
   for (Unt i = 0; i < hm->dictSize; i++) {
      if (hm->dict[i] != null) {
         return true;
      }
   }
   return false;
}

//}}}
//}}}
//{{{types

comptime typedef struct {
   int parenLvl; // level of the ()
   int curlyLvl; // level of the {}
   Boole metParens;
} ToplevelParse;

typedef enum {
   PUBLIC,
   PRIVATE,
   COMPTIME,
   INTERNAL,
   NONE_OR_ERROR
} AccessLevel;

comptime typedef struct {
   Unt tp : 6;
   Unt lenBts: 26;
   Unt startBt;
} Token;

comptime typedef struct { //:GenericMethod
   Slice name;
   Unt arity;
   LText* types;
} GenericMethod;

GEN_TYPE_L(comptime, Token);

GEN_TYPE_L(comptime, GenericMethod);
generic(2) GEN_create_L(private, GenericMethod);
generic(2) GEN_add_L(private, GenericMethod);

comptime typedef struct { //:GenParser
   LToken* tokens;
   S inp; //current position in "source"
   S source; //source code
} GenParser;


typedef enum {
   FUNCTION,
   TYPE,
   MACRO,
   CONSTANT
} ToplevelKind;

//A toplevel definition (
typedef struct { //:ToplevelThing
   Text c;
   ToplevelKind kind;
   AccessLevel acc;
} ToplevelThing;

pub typedef struct { //:FileParse
   Arr(ToplevelThing) c;
   Unt len;
   Unt cap;
   StringMap genericMethods; //method names to build _Generic() tables for.
                             //Values = indices into @generics
   LGenericMethod* generics;   //the types in _Generic() tables.
   Text source;
   Text existingForwDecls;
   FilePath fn;
   Arena* a;
} FileParse;

//}}}
//{{{@@forward declarations

private FileParse parseFile(Text fileContents, FilePath fn, Arena*);

private void addGeneric(OUT FileParse* r, GenParser g);

private Text writeGenerics(FileParse* r);

//}}}
//{{{utils

private Text
readSourceFile(FilePath fName) {
   FILE *file = fopen(fName.c, "r");
   if (!file)
      { return (Text){}; }

   // Go to the end of the file
   if (fseek(file, 0L, SEEK_END) != 0)
      { goto cleanup; }
   long fileSize = ftell(file);
   if (fileSize == -1)
      { goto cleanup; }

   // Go back to the start of the file
   if (fseek(file, 0L, SEEK_SET) != 0)
      { goto cleanup; }
      
   S result = malloc(fileSize + 1);

   // Read the entire file into memory
   Unt len = fread(OUT result, 1, fileSize, file);

   if (ferror(file) != 0 || len == 0) {
      fputs("Error reading file", stderr);
   } else {
      result[len] = '\0'; // Just to be safe
   }
   cleanup:
   fclose(file);
   return (Text){.c = result, .len = len};
}

private FileParse
processSourceFile(FilePath fname, Arena* a) {
   if (fname.len == 0 || fname.len == fname.dirLen) {
      return (FileParse){};
   }
   return parseFile(readSourceFile(fname), fname, a); 
}

//txt must be non-empty
private Unt
parseInteger(Text txt) {
   Unt powerOfTen = 1;
   Unt result = 0;
   for (Unt j = txt.len - 1; j < UNT; j--) {
      result += powerOfTen*(txt.c[j] - '0');
      powerOfTen *= 10;
   }
   return result;
}


private void
appendText(OUT S* w, Text txt) {
   memcpy(*w, txt.c, txt.len);
   *w += txt.len;
}

void __attribute__((noinline))
__bp() { // breakpoints for debugger
   ;
}

#define _bp(cond) if (cond) { __bp(); }

//}}}
//{{{lexical analysis

private S
skipNormalComment(S i) {
   S p = i;
   for (; p[0] != ZERO && p[0] != '\n'; p++) {
   }
   return p;
}

private S
skipMultilineComment(S i) {
   S p = i;
   for (; p[0] != ZERO && p[0] != '*' && p[1] != '/'; p++) {
   }
   return p;
}

private S
skipSpaces(S i) {
   S p = i;
   for (; p[0] == ' ' || p[0] == '\n'; p++) {
   }
   return p;
}

private Boole
isSpaceOrNewline(Byte c) {
   return c == ' ' || c == '\n';
}


private _pure Boole
startsWith(S big, Text prefix) {
   Unt i;
   for (i = 0; i < prefix.len && big[i] != ZERO; i++) {
      if (big[i] != prefix.c[i]) {
         return false;
      }
   }
   return i == prefix.len;
}

private _pure Boole
eq(Text a, Text b) {
   return a.len == b.len && memcmp(a.c, b.c, a.len) == 0;
}

//}}}
//{{{parsing
//{{{lexing generics
generic(2) GEN_create_L(private, Token);
generic(2) GEN_add_L(private, Token);

// Token types
// The following group of variants are transferred to the AST byte for byte, with no analysis
// Their values must exactly correspond with the initial group of variants in "Node"
// The largest value must be stored in "topVerbatimTokenVariant" constant
#define tokArity        2 //GEN_*()
#define tokMethod       4 //the argument signifying the method
#define tokType         5 //the argument holding a type chunk

#define errGenParser    1
#define errGenEndOfList 2 //reached the closing paren in an (a,b,c) list

typedef Unt (*GenParseFn)(OUT GenParser*);

private Unt
genMaybe(GenParseFn fn, OUT GenParser* gp) {
   fn(OUT gp);
   return 0;
}

private Unt
genConsumeKeyword(Text keyw, OUT GenParser* gp) {
   if (startsWith(gp->inp, keyw)) {
      gp->inp += keyw.len;
      return 0;
   } else {
      return errGenParser;
   }
}

private Unt
genLocalMacroMethod(OUT GenParser* g) {
   S p = g->inp;
   for (; p[0] != ZERO && p[0] != '_'; p++) {
   }
   if (p[0] == '_' && p - g->inp > 1) {
      add(g->tokens,
         ((Token){.tp = tokMethod, .startBt = g->inp - g->source, .lenBts = p - g->inp})
      );
      g->inp = p + 1;
      return 0;
   }
   return errGenParser;
}

private Unt
genLocalMacroTypeCon(OUT GenParser* g) {
   S p = g->inp;
   for (; p[0] != ZERO && p[0] != '('; p++) {
   }
   Unt len = p - g->inp;
   if (p[0] == '(' && len > 0) {
      add(g->tokens,
         ((Token){.tp = tokType, .startBt = g->inp - g->source, .lenBts = len})
      );
      g->inp = p + 1;
      return 0;
   } else {
      return errGenParser;
   }
}

private Boole
genIsNotTerminator(Byte b) {
   return !(b == ',' || b == ' ' || b == '\n' || b == ZERO);
}

private Unt
genArity(OUT GenParser* g) {
   S numStart = skipSpaces(g->inp);
   S p = numStart;
   for (; p[0] >= '0' && p[0] <= '9'; p++) {
   }
   if (p == numStart) {
      return errGenParser;
   }
   Token tok =  (Token){.tp = tokArity, .startBt = numStart - g->source, .lenBts = p - numStart};
   
   g->inp = skipSpaces(p);
   add(g->tokens, tok);
   return 0;
}

//Skip the first arg and ensure it's followed by a comma
private Unt
genAccessModifier(OUT GenParser* g) {
   S p = skipSpaces(g->inp);
   
   for (; genIsNotTerminator(p[0]); p++) {
   }
   p = skipSpaces(p);
   if (p[0] != ',' || p - g->inp == 0) {
      return errGenParser;
   }
   g->inp = p;
   return 0;
}

private Unt
genLocalMacroTypeArg(OUT GenParser* g) {
   if (g->inp[0] != ',') {
      return errGenParser;
   }
   
   S wordStart = skipSpaces(g->inp + 1); //+1 for the comma
   S p = wordStart;
   for (; genIsNotTerminator(p[0]) && p[0] != ')'; p++) {
   }
   Unt len = p - wordStart;
   if (len > 0) {
      add(g->tokens,
         ((Token){.tp = tokType, .startBt = wordStart - g->source, .lenBts = len})
      );
      g->inp = skipSpaces(p);
      return 0;
   }
   return errGenParser;
}

private Unt
genTypeArgs(OUT GenParser* g) {
   Unt errCode;
   for (errCode = 0; errCode == 0; ) {
      errCode = genLocalMacroTypeArg(OUT g);
   }
   return errCode == errGenEndOfList ? 0 : errCode;
}

//Start here: `method_Type(acc, typeArg, typeArg1)`
//Parse: `method`, `Type`, `typeArg` and `typeArg1`
private Unt
genLocalMacroImpl(OUT GenParser* g) {
   return
         genLocalMacroMethod(OUT g)
      || genLocalMacroTypeCon(OUT g)
      || genAccessModifier(OUT g)
      || genTypeArgs(OUT g);
}

//The `GEN_method_...` form which is used for local defs/decls
private Unt
genLocalMacro(OUT GenParser* g) {
   g->inp = skipSpaces(g->inp);
   return (genConsumeKeyword(tConst("GEN_"), OUT g) || genLocalMacroImpl(OUT g));
}

private Unt
genExternalMethod(OUT GenParser* g) {
   S wordStart = skipSpaces(g->inp);
   S p = wordStart;
   for (; genIsNotTerminator(p[0]); p++) {
   }
   Unt len = p - wordStart;
   if (len > 0) {
      add(g->tokens,
         ((Token){.tp = tokMethod, .startBt = wordStart - g->source, .lenBts = len})
      );
      g->inp = skipSpaces(p);
      return 0;
   } else {
      return errGenParser;
   }
}

//`generic(1, comptime, method, type)` `generic(1)`
//We are here ^                      or here     ^
//This is optional(there may be no arguments after arity)
private Unt
genExternalContent(OUT GenParser* g) {
   return
         genAccessModifier(OUT g)
      || genExternalMethod(OUT g)
      || genTypeArgs(OUT g);
}

private Unt
genClosingParen(OUT GenParser* g) {
   if (g->inp[0] != ')') {
      return errGenParser;
   }
   g->inp++;
   return 0;
}

//The `generic(1, comptime, method, type)` which is for using externally defined generic methods
//We are here  ^
private Unt
genExternalMacro(OUT GenParser* g) {
   return
         genArity(OUT g)
      || genMaybe(&genExternalContent, OUT g)
      || genClosingParen(OUT g);
}

//Parse a generic expression
//Parse `generic() GEN_add_L(private, Int)` or `generic(private, add, L, Int)` 
//We are here    ^
private void
genParse(OUT S* inp, OUT GenParser* g) {
   Unt errCode = 
         genExternalMacro(OUT g)
      || genMaybe(&genLocalMacro, OUT g);
      
   *inp = g->inp; 
   if (errCode > 0) {
      g->tokens = null;
   }
}

//}}}

private void
append(OUT FileParse* p, ToplevelThing new) {
   if (p->len == p->cap) {
      Arr(ToplevelThing) newContent = malloc(2*p->cap*sizeof(ToplevelThing));
      memcpy(OUT newContent, p->c, p->cap*sizeof(ToplevelThing));
      free(p->c);
      p->c = newContent;
      p->cap *= 2;
   } 
   p->c[p->len++] = new;
}

//#define macro()...
//Here    ^
private ToplevelThing
parseMacro(OUT S* inp, S i, AccessLevel accLevel) {
   S p = i + 8; //+8 for `#define `
   _bp(true);
   for (; p[0] != ZERO && p[0] != '\n'; p++) {
      if (p[0] == '\\') {
         for (; p[0] != ZERO && p[0] != '\n'; p++)
            {}
      }
   }
   *inp = p;
   return (ToplevelThing){(Text){i, p - i}, MACRO, accLevel};
}

//*inp is looking at the first non-space after "pub"/"private"/etc
private void
tryParseToplevelThing(OUT FileParse* p, OUT S* inp, AccessLevel accLevel) {
   int parenLvl = 0;
   int curlyLvl = 0;
   Boole metParens = false;
   S start = *inp;
   S i = start;
   
#define startsWithKeyword(kw) startsWith(i, tConst(kw)) && isSpaceOrNewline(i[sizeof(kw) - 1]) \
   && curlyLvl == 0 && parenLvl == 0
   
   //function = met parens and now see a {
   //constant = didn't meet parens and now see a =
   //type = didn't meet parens and now see a ;
   //typedef = might've met parens and we see a ;
   for (; *i != ZERO; i++) {
      switch (*i) {
      case '{': 
         if (metParens && curlyLvl == 0 && parenLvl == 0) {
            int len = i[-1] == ' ' ? (i - start - 1) : (i - start);
            append(
               OUT p,
               (ToplevelThing){.c = (Text){start, len}, .kind = FUNCTION, .acc = accLevel}
            );
            return;
         } else {
            curlyLvl++;
         }
         break;
      case '}':
         curlyLvl--;
         break;
      case '(':
         parenLvl++;
         if (curlyLvl == 0)
            metParens = true;
         break;
      case ')':
         parenLvl--;
         break;
      case '=':
         if (!metParens && curlyLvl == 0) {
            append(
               OUT p, 
               (ToplevelThing){.c = (Text){start, i - start}, .kind = CONSTANT, .acc = accLevel}
            );
            return;
         }
         break;
      case ';':
         if (curlyLvl == 0) {
            append(
               OUT p, 
               (ToplevelThing){.c = (Text){start, i - start}, .kind = TYPE, .acc = accLevel}
            );
            return;
         }
         break;
      case '#':
         if (startsWithKeyword("#define")) {
            append(
               OUT p,
               parseMacro(OUT inp, i, accLevel)
            ); 
            return;
         }
         break;
      case 's':
         if (startsWithKeyword("struct")) {
            i += 7; //CONSUME "struct "
         }
         break;
      case 'e':
         if (startsWithKeyword("enum")) {
            i += 5; //CONSUME "enum "
         }
         break;
      case 't':
         if (startsWithKeyword("typedef")) {
            i += 8; //CONSUME "typedef "
         }
         break;
      case '/':
         if (i[1] == '/') {
            i = skipNormalComment(i + 2);
         } ei (i[1] == '*') {
            i = skipMultilineComment(i + 2);
         }
         break;
      }
   }
   *inp = i;
   
#undef startsWithKeyword 
}

//private void
//printTokens(GenParser* g, S source) {
//   printf("Tokens:\n");
//   for (Unt i = 0; i < g->tokens->len; i++) {
//      Token t = g->tokens->c[i];
//      switch (t.tp) {
//      case tokArity:       printf("arity "); break;      
//      case tokMethod:      printf("method "); break;      
//      case tokType:        printf("Type "); break;      
//      }
//      fwrite(source + t.startBt, 1, t.lenBts, stdout);
//   }
//   printf("\n");
//}

//A `generic(a, b)` or `generic() GEN_` form
private void
tryParseGeneric(OUT FileParse* p, OUT S* inp, Arena* a) {
   GenParser parsedGenerics = (GenParser){
      .source = p->source.c, .tokens = create(L, Token)(4, a), .inp = *inp
   };
   
   genParse(OUT inp, OUT &parsedGenerics);
   
   if (!parsedGenerics.tokens) {
      return;
   }
   addGeneric(OUT p, parsedGenerics);
   
   //printTokens(&parsedGenerics, p->source.c);
}


comptime 
#define forwDeclMarker "@@"
comptime 
#define forwDeclPrologue "//{{" "{" forwDeclMarker "forward declarations"
comptime 
#define forwDeclEpilogue "//}}" "}"

private Text
determineExistingForwDecls(S markerLine) {
   S start = skipNormalComment(markerLine) + 1; //+1 for the newline
   S p = start;
   for (; p[0] != ZERO; p++) {
      if (startsWith(p, tConst(forwDeclEpilogue))) {
         break;
      }
   }
   return (Text){.c = start, .len = p - start};
}


private  FileParse
parseFile(Text source, FilePath fn, Arena* a) [[unsequenced]] {
   if (source.len == 0) {
      return (FileParse){};
   }
   
   StringMap* genMethods = create_StringMap(4, source.c, a);
   FileParse res = {
      .source = source, .fn = fn,
      .c = malloc(4*sizeof(ToplevelThing)), .len = 0, .cap = 4, .existingForwDecls = {}, 
      .genericMethods = *genMethods, .generics = create(L, GenericMethod)(4, a),
      .a = a
   };
   for (S inp = source.c; inp[0] != ZERO; inp++) {
      if (inp[0] == '\n') {
         if (inp[1] == 'p' || inp[1] == 'i') {
            inp++; //CONSUME the newline
            if (startsWith(inp, tConst("pub")) && isSpaceOrNewline(inp[3])) {
               inp = skipSpaces(inp + 3); //CONSUME "pub" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, PUBLIC);
            } ei (startsWith(inp, tConst("comptime")) && isSpaceOrNewline(inp[8])) {
               inp = skipSpaces(inp + 11); //CONSUME "comptime" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, COMPTIME);
            } ei (startsWith(inp, tConst("private")) && isSpaceOrNewline(inp[7])) {
               inp = skipSpaces(inp + 7); //CONSUME "private" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, PRIVATE);
            } ei (startsWith(inp, tConst("internal")) && isSpaceOrNewline(inp[8])) {
               inp = skipSpaces(inp + 8); //CONSUME "internal" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, INTERNAL);
            }
         } ei (inp[1] == 'g') { 
            inp++; //CONSUME the newline
            if (startsWith(inp, tConst("generic("))) {
               inp += 8;
               tryParseGeneric(OUT &res, OUT &inp, a);
            } 
         } ei (inp[1] == '/' && inp[2] == '/') {
            if (inp[3] == '{' && startsWith(inp + 1, tConst("//{{" "{" forwDeclMarker))) {
               //found forward declarations block. It will be written to, and no need to read it
               
               res.existingForwDecls = determineExistingForwDecls(inp + 8);
               if (res.existingForwDecls.len > 0) {
                  //skipping the forward declarations block as it has nothing interesting
                  inp = res.existingForwDecls.c + res.existingForwDecls.len + 3;
               }
            } else {
               inp = skipNormalComment(inp);
            }
         }
      }
   }
   
   return res;
}

private Unt
toplevelLen(ToplevelThing* t) {
   return t->c.len + 2; //+2 for the semicolon & newline char
}

private void
toplevelWrite(OUT S* w, ToplevelThing* t) {
   if (t->kind == FUNCTION) { //Newline->space for functions to fit in 1 line if they were 2 lines
      Unt firstNewline;
      for (firstNewline = 1; firstNewline < t->c.len; firstNewline++) {
         if (t->c.c[firstNewline] == '\n') {
            break;
         }
      }
      if (firstNewline < t->c.len - 1) {
         Unt restLen = t->c.len - firstNewline - 1;
         memcpy(*w, t->c.c, firstNewline);
         (*w)[firstNewline] = ' ';
         *w += firstNewline + 1;
         
         memcpy(*w, t->c.c + firstNewline + 1, restLen);
         *w += restLen;
         (*w)[0] = ';';
         (*w)[1] = '\n';
         *w += 2;
         return;
      }
   }
   memcpy(*w, t->c.c, t->c.len);
   *w += t->c.len;
   (*w)[0] = ';';
   (*w)[1] = '\n';
   *w += 2;
}

//Create a string like `LLText`
private Text
glueGenericType(Arr(Token) typeTokens, Unt count, S source, Arena* a) {
   Unt len = 0;
   for (Unt i = 0; i < count; i++) {
      len += typeTokens[i].lenBts;
   }
   S result = (S)allocateArray(len + 1, Byte, a);
   result[len] = ZERO;
   
   S w = result;
   for (Unt i = 0; i < count; i++) {
      memcpy(w, source + typeTokens[i].startBt, typeTokens[i].lenBts);
      w += typeTokens[i].lenBts;
   }
   return (Text){result, len};
}

private void
addGeneric(OUT FileParse* r, GenParser g) {
   Unt indArity = UNT;
   Unt indMethod = UNT;
   Unt indTypeStart = UNT;
   for (Unt i = 0; i < g.tokens->len; i++) {
      if (g.tokens->c[i].tp == tokArity) {
         indArity = i;
      } ei (g.tokens->c[i].tp == tokMethod) {
         indMethod = i;
      } ei (g.tokens->c[i].tp == tokType) {
         indTypeStart = i;
         break;
      }
   }
   if (indArity == UNT || indMethod == UNT || indTypeStart == UNT || indMethod > indTypeStart) {
      return;
   }
   for (Unt i = indTypeStart; i < g.tokens->len; i++) {
      if (g.tokens->c[i].tp != tokType) {
         return;
      }
   }
   
   Slice methName = (Slice){g.tokens->c[indMethod].startBt, g.tokens->c[indMethod].lenBts};
   Text type = glueGenericType(
         g.tokens->c + indTypeStart, g.tokens->len - indTypeStart, r->source.c, r->a
   );
   
   Unt ind = get_StringMap(methName, &r->genericMethods);
   if (ind != UNT) {
      add(r->generics->c[ind].types, type);
   } else {
      Token arityTk = g.tokens->c[indArity];
      Unt arity = parseInteger((Text){r->source.c + arityTk.startBt, arityTk.lenBts });
      
      LText* typeList = create(L, Text)(2, r->a);
      add(typeList, type);
      GenericMethod newMethod = (GenericMethod){methName, arity, typeList};
      
      put_StringMap(methName, r->generics->len, OUT &r->genericMethods);
      add(r->generics, newMethod);
   }
}

//}}}
//{{{writing

//Returns allocated string, caller must free it
private Text
buildPublicHeader(FileParse* r) [[unsequenced]] {
   Unt totalLen = 0;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PUBLIC) {
         totalLen += toplevelLen(r->c + i); 
      }
   }
   
   S newContent = malloc(totalLen + 1);
   newContent[totalLen] = ZERO;
   S w = newContent;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PUBLIC) {
         toplevelWrite(OUT &w, r->c + i);
      }
   }
   
   if (w - newContent != totalLen) {
      printf("ERROR in public header: totalLen %d but wrote only %d", totalLen, (Unt)(w - newContent));
   }
   return (Text){newContent, totalLen};
}

[[nodiscard]]
private Boole
dirExists(S path) {
    struct stat statResult;

    // stat() returns 0 on success
    if (stat(path, OUT &statResult) == 0) {
        // Check if the path is a directory
        return S_ISDIR(statResult.st_mode);
    }

    // Path does not exist or is not accessible
    return false;
}

private S
determinePublicName(FileParse* r, Boole isInternal, NULLABLE S subdir) [[unsequenced]] {
   S publicName;
   Unt internalLen = isInternal ? 9 : 0;
   Unt len;
   if (subdir) {
      Unt subdirLen = strlen(subdir);
      len = r->fn.len + 1 + internalLen + subdirLen;
      publicName = malloc(len + 1);
      publicName[len] = ZERO;
      memcpy(publicName, r->fn.c, r->fn.dirLen);
      memcpy(publicName + r->fn.dirLen, subdir, subdirLen);
      
      //check if dir exists
      publicName[r->fn.dirLen + subdirLen] = ZERO;
      if (!dirExists(publicName)) {
         printf("BetterC error: dir doesn't exist\n");
         printf("||%s||\n", publicName);
         exit(1);
      }
      
      publicName[r->fn.dirLen + subdirLen] = '/';
      memcpy(
            OUT publicName + r->fn.dirLen + subdirLen + 1, 
            r->fn.c + r->fn.dirLen, 
            r->fn.len - r->fn.dirLen - 1 //-1 for the to-be overwritten "c" at the end
      );
      if (isInternal) {
         memcpy(publicName + r->fn.len - 1, "internal.", internalLen);
      }
      publicName[len - 1] = 'h';
   } else {
      len = r->fn.len + internalLen;
      publicName = malloc(len + 1);
      publicName[len] = ZERO;
      memcpy(publicName, r->fn.c, r->fn.len - 1);
      if (isInternal) {
         memcpy(publicName + r->fn.len - 1, "internal.", internalLen);
      }
      publicName[len - 1] = 'h';
   }
   return publicName;
}

private void
writePublicHeader(FileParse* r, S publicName) {
   Text publicContent = buildPublicHeader(r);
   
   FILE* out = fopen(publicName, "w");
   fputs(publicContent.c, out);
   fclose(out);
   free(publicName);
   free(publicContent.c);
}

//Return an empty Text. This is the place where forward declarations will be inserted
private Text
writeDeterminePlaceForForwDecls(FileParse* r) {
   Unt latestType = 0;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].kind == TYPE) {
         Unt end = (r->c[i].c.c - r->source.c) + r->c[i].c.len + 1;
         if (end > latestType) {
            latestType = end;
         }
      }
   }
   return (Text){r->source.c + latestType, 0};
}

private S
buildFileImpl(
   FileParse* r, Text existingDecls, Text genericMacros, Unt beforeLen, Unt afterLen, Unt totalLen
) {
   S newContent = malloc(totalLen + 1);
   newContent[totalLen] = ZERO;
   S w = newContent;
   memcpy(w, r->source.c, beforeLen);
   w += beforeLen;
   
   if (existingDecls.len == 0) {
      w[0] = '\n';
      w++;
      memcpy(w, forwDeclPrologue, sizeof(forwDeclPrologue) - 1);
      w += sizeof(forwDeclPrologue);
      w[-1] = '\n';
   }
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PRIVATE && r->c[i].kind == FUNCTION) {
         memcpy(w, "private ", 8);
         w += 8;
         toplevelWrite(OUT &w, r->c + i);
      }
   }
   _bp(true);
   if (genericMacros.len > 0) {
      memcpy(w, genericMacros.c, genericMacros.len);
      w += genericMacros.len;
   }
   
   if (existingDecls.len == 0) {
      memcpy(w, forwDeclEpilogue, sizeof(forwDeclEpilogue) - 1);
      w += sizeof(forwDeclEpilogue);
      w[-1] = '\n';
   }
   memcpy(w, r->source.c + beforeLen + existingDecls.len, afterLen);
   w += afterLen;
   
   if (w - newContent != totalLen) {
      printf("ERROR totalLen %d but wrote only %d", totalLen, (Unt)(w - newContent));
   }
   
   return newContent;
}

private Unt
calcForwardDeclLen(FileParse* r) {
   Unt fwDeclLen = 0;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PRIVATE && r->c[i].kind == FUNCTION) {
         fwDeclLen += toplevelLen(r->c + i) + 8; //+8 for the "private "
      }
   }
   
   return fwDeclLen;
}

//Return new allocated string with file content
private S
buildFileWithForwDecls(FileParse* r) {
   Text existingDecls = r->existingForwDecls;
   if (existingDecls.len == 0) {
      existingDecls = writeDeterminePlaceForForwDecls(r);
   }
   
   
   Unt fwDeclLen = calcForwardDeclLen(r);
   Text genericMacros = (Text){null, 0};
   if (isNonempty_StringMap(&r->genericMethods)) {
      genericMacros = writeGenerics(r);
      fwDeclLen += genericMacros.len;
   }
   
   Unt totalLen;
   Unt beforeLen = existingDecls.c - r->source.c;
   Unt afterLen; 
   if (existingDecls.len == 0) {
      afterLen = r->source.len - beforeLen;
      totalLen = beforeLen + (sizeof(forwDeclPrologue) + 1) //"+ 1" because also two newline chars
         + fwDeclLen 
         + (sizeof(forwDeclEpilogue)) + afterLen;           //No "- 1" because also a newline char
   } else {
      afterLen = r->source.len - beforeLen - existingDecls.len;
      totalLen = beforeLen + fwDeclLen + afterLen;
   }
   
   return buildFileImpl(r, existingDecls, genericMacros, beforeLen, afterLen, totalLen);
}

private void
writeForwDecl(FileParse* r) {
   S newContent = buildFileWithForwDecls(r);
   
   FILE* out = fopen(r->fn.c, "w");
   fputs(newContent, out);
   fclose(out);
   free(newContent);
}

//{{{generics

private Unt
calcGenericLen(GenericMethod m) {
   Unt n = m.types->len;
   Unt typesLen = 0;
   for (Unt i = 0; i < m.types->len; i++) {
      typesLen += m.types->c[i].len;
   }
   return 8 // #define  
      + 16 // the tail of the first line, with the \n
      + 2*3*m.arity //param list, twice. 3 is parens and first param
      + m.name.len*(n + 1)
      + 2*typesLen
      + (3*n - 1) //commas, backslashes and newlines for all types (-1 for the last comma)
      + 3*(n + 1) //indentation (3 spaces for every line)
      + 4*n //the middles (i.e. `*: ` and `_`)
      + 2;   //closing paren and newline char
}

private Unt
calcGenericsLen(LGenericMethod* methods) {
   Unt res = 0;
   for (Unt i = 0; i < methods->len; i++) {
      res += calcGenericLen(methods->c[i]);
   }
   return res;
}

private S
writeGenericParamList(S w, Unt arity) {
   static Byte alfabet[] = "abcdefghijklmnopqrstuvwxyz";
   w[0] = '(';
   w[1] = 'a';
   w += 2;
   for (Unt i = 1; i < arity; i++) {
      w[0] = ',';
      w[1] = ' ';
      w[2] = alfabet[i % 26];
      w += 3;
   }
   w[0] = ')';
   return w + 1;
}

private void
writeGeneric(OUT S* w, GenericMethod method, S source) {
   appendText(w, tConst("#define "));
   appendText(w, textOfSlice(method.name, source));
   *w = writeGenericParamList(*w, method.arity);
   appendText(w, tConst(" _Generic((a),\\\n"));
   for (Unt i = 0; i < method.types->len; i++) {
      appendText(w, tConst("   "));
      appendText(w, method.types->c[i]);
      appendText(w, tConst("*: "));
      appendText(w, textOfSlice(method.name, source));
      appendText(w, tConst("_"));
      appendText(w, method.types->c[i]);
      if (i == method.types->len - 1) {
         appendText(w, tConst("\\""\n"));
      } else {
         appendText(w, tConst(",\\""\n"));
      }
   }
   
   appendText(w, tConst("   )"));
   *w = writeGenericParamList(*w, method.arity);
   *w[0] = '\n';
   (*w)++;
}

private Text
writeGenerics(FileParse* r) {
   Unt len = calcGenericsLen(r->generics);
   S res = allocateOnArena(len + 1, r->a);
   res[len] = ZERO;
   S w = res;
   for (Unt i = 0; i < r->generics->len; i++) {
      writeGeneric(OUT &w, r->generics->c[i], r->source.c);
   }
   
   return (Text){res, len};
}


//}}}

private void 
writeResults(FileParse* r, NULLABLE S subdir) {
   Unt countPublics = 0;
   Unt countPrivateFns = 0; //functions only, only they need forward declarations
   Unt countInternals = 0;
   for (Unt i = 0; i < r->len; i++) {
      ToplevelThing thing = r->c[i];
      
      switch(thing.acc) {
      case PUBLIC: countPublics++; break;
      case PRIVATE: 
         if (thing.kind == FUNCTION)
            countPrivateFns++; 
         break;
      case INTERNAL: countInternals++; break;
      default:
      }
   }
   if (countPublics > 0) {
      S publicName = determinePublicName(r, false, subdir);
      writePublicHeader(r, publicName);
   }
   if (countInternals > 0) {
      S internalName = determinePublicName(r, true, subdir);
      writePublicHeader(r, internalName);
   }
   if (countPrivateFns > 0) {
      //Need to rewrite the source file (.c) to add/update the forward fn declarations
      writeForwDecl(r);
   }
}

//}}}
//{{{misc

private void
printUsage() {
    printf("Example usage:\n");
    printf("betterc source/file.c\n");
    printf("betterc -d headers source/file.c\n");
    printf("\n");
}

comptime typedef struct {
   S fn;
   NULLABLE S subdir;
   Boole correct;
} CommLine;

//Return subdir if it's specified, or null
private CommLine
parseCommLine(int argc, char** argv, int) {
   CommLine res = {};
   if (argc == 1) {
      printf("BetterC: Must name an input file!\n");
   } ei (eq(text(argv[1]), tConst("-d")) && argc == 4) {
      res.subdir = argv[2];
      res.fn = argv[3];
      res.correct = true;
   } ei (argc == 2) {
      res.fn = argv[2];
      res.correct = true;
   } else {
      printf("BetterC: erroneous arguments\n");
   }
   return res;
}

//}}}

pub int
main(int argc, char** argv) {
   CommLine commLine = parseCommLine(argc, argv, 5);
   if (!commLine.correct) { 
      printUsage();
      return 1;
   } 
   
   FilePath inpFile = filePath(commLine.fn);
   
   Arena* a = createArena();
   
   FileParse parseRes = processSourceFile(inpFile, a); 
   writeResults(&parseRes, commLine.subdir);
   
   deleteArena(a); 
}

