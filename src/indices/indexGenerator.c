// indexGenerator.c: helper program for `make indices`
// This jig here outputs the updated indices for runtime action, command searchin'
// Author: Egor Sozonov

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define private static
#define ZERO 0
#define Arr(T) T*
typedef int32_t Int;
typedef uint32_t Unt;
typedef uint16_t Short;
#define SHORT 65535 // 2**16 - 1
#define ei else if
typedef int64_t Long;
typedef uint64_t Ulong;
typedef unsigned char Byte;
typedef Arr(Byte) CS;
#define EXTRA_H
#include "../generic.h"
#undef EXTRA

#include "../actions.h"

#define DECLARE_COMMANDS_ENUM
#include "../commands.h"
#undef DECLARE_COMMANDS_ENUM
#define DECLARE_COMMANDS_FOR_INDEXING
#include "../commands.h"
#undef DECLARE_COMMANDS_FOR_INDEXING


//{{{action indices

typedef struct {
   int ind;
   int actionChar;
} ActionPair;

int actionComparer(void const* a, void const* b ){
   return ((ActionPair*)a)->actionChar - ((ActionPair*)b)->actionChar; 
}

void
createActionIndices() {
   FILE* tgt = fopen("src/indices/actions.h", "w");
   if (tgt == NULL) {
      perror("Couldn't open src/indices/actions.h for writing");
      exit(1);
   }
   
   fprintf(tgt, "%s",
      "//Automatically generated code by the src/indices/indexGenerator.c script.\n"
      "//\n"
      "//Table giving the index in actions[] to lookup based on the action character\n"
      "//\n"
      "//actionIndices[<normal mode command character>] => actions[] index\n"
      "static const unsigned short actionIndices[] = {\n"
   ); 
   ActionPair sortedArray[ACTIONS_SIZE];
   
   // Special keys are negative, use the negated value for sorting.
   for (int i = 0; i < ACTIONS_SIZE; i++) {
      sortedArray[i] = (ActionPair){
         .ind = i, .actionChar = actions[i] < 0 ? -actions[i] : actions[i]
      };
   }
   
   int maxLinear = 0;
   qsort(sortedArray, ACTIONS_SIZE, sizeof(ActionPair), &actionComparer);
   
   for (int i = 0; i < ACTIONS_SIZE; i++) {
      if (sortedArray[i].actionChar != i) {
         maxLinear = i - 1;
         break;
      }
   }
   
   for (int i = 0; i < ACTIONS_SIZE - 1; i++) {
      fprintf(tgt, "   %d, // %d\n", sortedArray[i].ind, sortedArray[i].actionChar);
   }
   fprintf(tgt,
      "   %d // %d\n", sortedArray[ACTIONS_SIZE - 1].ind, sortedArray[ACTIONS_SIZE - 1].actionChar
   );
   
   fprintf(tgt, "};\n\n");
   
   fprintf(tgt, "%s",    
      "//The highest index for which\n"
      "//actions[idx].actionChar == idx\n"
   );
   
   fprintf(tgt, "static int const actionsMaxLinear = %d;", maxLinear);
   fclose(tgt);
}

//}}}
//{{{command indices

void
printInd1(int v1, int v0, FILE* tgt) {
   if (v1 > 0) {
      int diff = v1 - v0;
      if (diff > 9) {
         fprintf(tgt, "%d, ", diff);
      } else {
         fprintf(tgt, " %d, ", diff);
      }
   } else {
      fprintf(tgt, " 0, ");
   } 
}

void
printLastInd1(int v1, int v0, int letter, FILE* tgt) {
   if (v1 > 0) {
      int diff = v1 - v0;
      if (diff > 9) {
         fprintf(tgt, "%d }, //%c\n ", diff, letter);
      } else {
         fprintf(tgt, " %d }, //%c\n ", diff, letter);
      }
   } else {
      fprintf(tgt, " 0 }, //%c\n", letter);
   } 
}

void
printLastLastInd1(int v1, int v0, int letter, FILE* tgt) {
   if (v1 > 0) {
      int diff = v1 - v0;
      
      if (diff > 9) {
         fprintf(tgt, "%d } //%c\n ", diff, letter);
      } else {
         fprintf(tgt, " %d } //%c\n ", diff, letter);
      }
   } else {
      fprintf(tgt, " 0 }  //%c\n", letter);
   } 
}

int
createCommandIndices() {
   FILE* tgt = fopen("src/indices/commands.h", "w");
   if (tgt == NULL) {
      perror("Couldn't open src/indices/commands.h for writing!");
      exit(1);
   }

   int i = COUNT_COMMANDS - 1;
   for (; commands[i].name[0] < asciiA || commands[i].name[0] > (asciiA + 26); i--)
      {}
   int const countLowercaseCommands = i + 1; // upper case commands are at end and not indexed
   
   // validate flags for all commands 
   int len = 0;
   for (int i = 0; i < COUNT_COMMANDS; i++) {
      CommandForIndexing cd = commands[i];
      if (cd.flags & RANGE && cd.addressKind == ADDR_NONE) {
         fprintf(stderr, "Command %s uses RANGE with ADDR_NONE", cd.name);
         exit(1);
      } else if (!(cd.flags & RANGE) && cd.addressKind != ADDR_NONE) {
         fprintf(stderr, "Command %s is missing ADDR_NONE", cd.name);
         exit(1);
      } else if (
            cd.flags & DFLALL && (cd.addressKind == ADDR_OTHER || cd.addressKind == ADDR_NONE)
      ) {
         fprintf(stderr, "Missing misplaced DFLALL in command %s", cd.name);
         exit(1);
      }
      len++;
   }
   
   int indices0[26];
   memset(indices0, 0, 26*4);
   int indices1[26][26];
   memset(indices1, 0, 26*26*4);
   
   int prevC0 = -1;
   char prevC1 = 0;
   for (int i = 0; i < countLowercaseCommands; i++) {
      int c0 = commands[i].name[0] - asciiA;
      char c1 = commands[i].name[1];
      if (c0 != prevC0) {
         indices0[c0] = i;
         prevC0 = c0;
         
         if (c1 >= 'a' && c1 <= 'z') {
            indices1[c0][c1 - asciiA] = i; 
            prevC1 = c1;
         }
      } else if (c1 >= 'a' && c1 <= 'z' && c1 != prevC1) { 
         // first letter is same, but 2nd may be new
         indices1[prevC0][c1 - asciiA] = i; 
         prevC1 = c1;
      } 
   }
   
   
   fprintf(tgt, "%s",
      "// Automatically generated code by the script src/indices/indexGenerator.c\n"
      "// Table giving the index of the first command in commands[] to lookup\n"
      "// based on the first letter of the command.\n"
      "static const unsigned short commandIndices0[26] =\n"
      "{\n"
   ); 
   for (int i = asciiA; i < asciiA + 25; i++) {
      fprintf(tgt, "   %d, // %c\n", indices0[i - asciiA], i);
   }
   fprintf(tgt, "   %d  // z\n", indices0[25]);
   
   
   fprintf(tgt, "%s",   
      "};\n"
      "\n"
      "// Table giving the index of the first command in cmdnames[] to lookup\n"
      "// based on the first 2 letters of a command.\n"
      "// Values in commandIndices0[c0][c1] are relative to commandIndices0[c0] so that they\n"
      "// fit in a byte.\n"
      "\n"
      "static const unsigned char commandIndices1[26][26] =\n"
      "{ //  a   b   c   d   e   f   g   h   i   j   k   l   m   n   o   p   q   r   "
         "s   t   u   v   w   x   y   z \n"
   ); 
   
   for (int i = 0; i < 25; i++) {
      fprintf(tgt, "   { ");
      for (int j = 0; j < 25; j++) {
         printInd1(indices1[i][j], indices0[i], tgt);
      }
      printLastInd1(indices1[i][25], indices0[i], i + asciiA, tgt);
   }
   
   fprintf(tgt, "   { ");
   for (int j = 0; j < 25; j++) {
      printInd1(indices1[25][j], indices0[25], tgt);
   }
   printLastLastInd1(indices1[25][25], indices0[25], 25 + asciiA, tgt);
   
   fprintf(tgt, "};\n\n");
   
   fprintf(tgt, "static const int generatedCommandCount = %d;\n", COUNT_COMMANDS);
   
   fclose(tgt); 
   return 1;
}

//}}}
//{{{option indices

#define OPTIONS_NAMES

CS OPT_GLOBAL_NAMES[] = {
#define OPTIONS_DEF_GLOBAL
#include "../defoption.h"
#undef OPTIONS_DEF_GLOBAL
};

CS OPT_PORTAL_NAMES[] = {
#define OPTIONS_DEF_PORTAL
#include "../defoption.h"
#undef OPTIONS_DEF_PORTAL
};

CS OPT_BOOK_NAMES[] = {
#define OPTIONS_DEF_BOOK
#include "../defoption.h"
#undef OPTIONS_DEF_BOOK
};

#undef OPTIONS_NAMES


typedef struct {
   CS name; 
   Unt index;
} NameIndex;

private int
nameIndexComparer(void const* a0, void const* b0) {
   NameIndex* a = (NameIndex*)a0;
   NameIndex* b = (NameIndex*)b0;
   return strcmp((char const*)a->name, (char const*)b->name);
}

private Byte
toLowerAscii(Byte a) {
   if (a >= 'a') {
      return a;
   } else {
      return a + ('a' - 'A');
   }
}

void
createOptionIndices() {
   FILE* tgt = fopen("src/indices/options.h", "w");
   if (!tgt) {
      perror("Couldn't open src/indices/options.h for writing");
      exit(1);
   }
   
   //{{{normal options 
   
   int countGlobal = sizeof(OPT_GLOBAL_NAMES)/sizeof(CS); 
   int countPortal = sizeof(OPT_PORTAL_NAMES)/sizeof(CS); 
   int countBook = sizeof(OPT_BOOK_NAMES)/sizeof(CS); 
   int count = countGlobal + countPortal + countBook;
   Arr(NameIndex) nameIndices = malloc(count*sizeof(NameIndex));
   int j = 0;
   for (int i = 0; i < countGlobal; i++, j++) {
      nameIndices[j] = (NameIndex){OPT_GLOBAL_NAMES[i], i};
   }
   for (int i = 0; i < countPortal; i++, j++) {
      nameIndices[j] = (NameIndex){OPT_PORTAL_NAMES[i], i + SHORT + 1};
   }
   for (int i = 0; i < countBook; i++, j++) {
      nameIndices[j] = (NameIndex){OPT_BOOK_NAMES[i], i + 2*(SHORT + 1)};
   }
   qsort(nameIndices, count, sizeof(NameIndex), &nameIndexComparer);
   
   fprintf(tgt, "%s",
      "//Automatically generated code by the script src/indices/indexGenerator.c\n"
      "typedef struct {CS name; Unt index;} NameIndex;\n"
   ); 
   fprintf(tgt, "static NameIndex const NAME_INDICES[%d] = {\n", count);
   for (int i = 0; i < count; i++) {
      fprintf(tgt, "{(Byte*)\"%s\", %u},\n", nameIndices[i].name, nameIndices[i].index);
   }
   fprintf(tgt, "%s",   
      "};\n"
      "\n"
   ); 
   
   int firstLetterIndices[27];
   firstLetterIndices[0] = 0;
   firstLetterIndices[26] = count;
   fprintf(tgt, "static int const FIRST_LETTER_INDICES[27] = {\n");
   j = 1;
   Byte expecting = 'b';
   for (int i = 1; i < count; i++) {
      Byte nameChar = nameIndices[i].name[0];
      if (nameChar == expecting) {
         firstLetterIndices[j] = i;
         j++;
         expecting = 'a' + j;
      } ei (nameChar > expecting) {
         firstLetterIndices[j] = i;
         j++;
         for (; j <= nameChar - 'a'; j++) {
            firstLetterIndices[j] = i;
         } 
         expecting = j + 'a';
      }
   }
   for (int k = 25; k >= j; k--) {
      firstLetterIndices[k] = firstLetterIndices[k + 1];
   }
   for (j = 0; j < 27; j++) {
      fprintf(tgt, "%d,\n", firstLetterIndices[j]);
   } 
   fprintf(tgt, "%s",   
      "};\n"
      "\n"
   ); 
   
   //}}}   
   
   fprintf(tgt, "#define OPTION_COUNT %d\n", count);
   fclose(tgt); 
   
   FILE* countsTgt = fopen("src/indices/optionCounts.h", "w");
   if (!countsTgt) {
      perror("Couldn't open src/indices/optionCounts.h for writing");
      exit(1);
   }
   fprintf(tgt, "#define OPTION_GLOBAL_COUNT %d\n", countGlobal);
   fprintf(tgt, "#define OPTION_PORTAL_COUNT %d\n", countPortal);
   fprintf(tgt, "#define OPTION_BOOK_COUNT %d\n", countBook);
   fprintf(tgt, "\n");
   fclose(tgt); 
}

//}}}

int main(int argc, char** argv) {
   if (argc != 2) {
      return 1;
   }
   if (argv[1][0] == 'a') {
      createActionIndices();
   } else if (argv[1][0] == 'c') {
      createCommandIndices();
   } else if (argv[1][0] == 'o') {
      createOptionIndices();
   }
   return 0;
}
