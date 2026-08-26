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
#define privateComp //compile-time private (doesn't affect linking) - for types and macros

typedef unsigned char Byte;

#define _pure __attribute__((pure))

#define ZERO 0
#define false 0
#define true 1
#define ei else if
#define Arr(T) T*
#define null nullptr
#define UNT 4294967295       //2**32 - 1

typedef char* CS;
typedef char Boole;
typedef uint32_t Unt;


privateComp typedef struct {
   CS c;
   int len;
} Text;

private _pure Text
text(CS s) {
   return (Text){s, strlen(s)};
}

__attribute__((error("Expression is not a constant"))) Text
notconst(void);

#define tConst(s) __builtin_constant_p(s) ? text(s) : notconst()

pub typedef struct {
   CS msg;
} Error;

privateComp typedef struct {
   CS c;       //absolute filename
   Unt dirLen; //length including the last '/' in the filename
   Unt len;    //full length (so that the short name lies in [dirLen + 1; len)
} FilePath;

private FilePath
filePath(CS rawFname) {
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
//{{{types

typedef struct {
   int parenLvl; // level of the ()
   int curlyLvl; // level of the {}
   Boole metParens;
} ToplevelParse;

typedef enum {
   PUBLIC,
   PRIVATE,
   PRIVATE_COMP,
   INTERNAL,
   NONE_OR_ERROR
} AccessLevel;

typedef enum {
   FUNCTION,
   TYPE,
   MACRO,
   CONSTANT
} ToplevelKind;

typedef struct {
   Text c;
   ToplevelKind kind;
   AccessLevel acc;
} ToplevelThing;

pub typedef struct {
   Arr(ToplevelThing) c;
   int len;
   int cap;
   Text source;
   Text existingForwDecls;
   FilePath fn;
} FileParse;

//}}}
//{{{@@forward declarations

private FileParse parseFile(Text fileContents, FilePath fn);

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
      
   CS result = malloc(fileSize + 1);

   // Read the entire file into memory
   Unt len = fread(result, 1, fileSize, file);

   if (ferror(file) != 0 ) {
      fputs("Error reading file", stderr);
   } else {
      result[len] = '\0'; // Just to be safe
   }
   cleanup:
   fclose(file);
   return (Text){.c = result, .len = len};
}

private FileParse
processSourceFile(FilePath fname) {
   if (fname.len == 0 || fname.len == fname.dirLen) {
      return (FileParse){};
   }
   return parseFile(readSourceFile(fname), fname); 
}

void __attribute__((noinline))
__bp() { // breakpoints for debugger
   ;
}

#define _bp(cond) if (cond) { __bp(); }

//}}}
//{{{lexical analysis

private CS
skipNormalComment(CS i) {
   CS p = i;
   for (; p[0] != ZERO && p[0] != '\n'; p++) {
   }
   return p;
}

private CS
skipMultilineComment(CS i) {
   CS p = i;
   for (; p[0] != ZERO && p[0] != '*' && p[1] != '/'; p++) {
   }
   return p;
}

private CS
skipSpaces(CS i) {
   CS p = i;
   for (; p[0] == ' ' || p[0] == '\n'; p++) {
   }
   return p;
}

private Boole
isSpaceOrNewline(Byte c) {
   return c == ' ' || c == '\n';
}


private _pure Boole
startsWith(CS big, Text prefix) {
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

//function = met parens and now see a {
//constant = didn't meet parens and now see a =
//type = didn't meet parens and now see a ;
//typedef = might've met parens and we see a ;
private void
tryParseToplevelThing(OUT FileParse* p, OUT CS* inp, AccessLevel accLevel) {
   int parenLvl = 0;
   int curlyLvl = 0;
   Boole metParens = false;
   CS start = *inp;
   CS i = start;
   ToplevelKind kind = FUNCTION;
   
#define startsWithKeyword(kw) startsWith(i, tConst(kw)) && isSpaceOrNewline(i[sizeof(kw) - 1]) \
   && curlyLvl == 0 && parenLvl == 0
   
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
            kind = MACRO;
            i += 8; //CONSUME "#define "
         }
         break;
      case 's':
         if (startsWithKeyword("struct")) {
            kind = TYPE;
            i += 7; //CONSUME "struct "
         }
         break;
      case 'e':
         if (startsWithKeyword("enum")) {
            kind = TYPE;
            i += 5; //CONSUME "enum "
         }
         break;
      case 't':
         if (startsWithKeyword("typedef")) {
            kind = TYPE;
            i += 8; //CONSUME "typedef "
         }
         break;
      case '\\':
         //skip newline in macros
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
}

privateComp 
#define forwDeclMarker "@@"
privateComp 
#define forwDeclPrologue "//{{" "{" forwDeclMarker "forward declarations"
privateComp 
#define forwDeclEpilogue "//}}" "}"

private Text
determineExistingForwDecls(CS markerLine) {
   CS start = skipNormalComment(markerLine) + 1; //+1 for the newline
   CS p = start;
   for (; p[0] != ZERO; p++) {
      if (startsWith(p, tConst(forwDeclEpilogue))) {
         break;
      }
      //if (p[0] == '/' && p[1] == '/' && p[2] == '}' && p[3] == '}' && p[4] == '}') {
      //   break;
      //} 
   }
   return (Text){.c = start, .len = p - start};
}

private FileParse
parseFile(Text source, FilePath fn) {
   if (source.len == 0) {
      return (FileParse){};
   }
   
   FileParse res = {
      .source = source, .fn = fn,
      .c = malloc(4*sizeof(ToplevelThing)), .len = 0, .cap = 4, .existingForwDecls = {},
   };
   
   for (CS inp = source.c; inp[0] != ZERO; inp++) {
      if (inp[0] == '\n') {
         if (inp[1] == 'p' || inp[1] == 'i') {
            inp++; //CONSUME the newline
            if (startsWith(inp, tConst("pub")) && isSpaceOrNewline(inp[3])) {
               inp = skipSpaces(inp + 3); //CONSUME "pub" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, PUBLIC);
            } ei (startsWith(inp, tConst("privateComp")) && isSpaceOrNewline(inp[11])) {
               inp = skipSpaces(inp + 11); //CONSUME "privateComp" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, PRIVATE_COMP);
            } ei (startsWith(inp, tConst("private")) && isSpaceOrNewline(inp[7])) {
               inp = skipSpaces(inp + 7); //CONSUME "private" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, PRIVATE);
            } ei (startsWith(inp, tConst("internal")) && isSpaceOrNewline(inp[8])) {
               inp = skipSpaces(inp + 8); //CONSUME "internal" and spaces after it
               tryParseToplevelThing(OUT &res, OUT &inp, INTERNAL);
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

//}}}
//{{{writing

private Text
buildPublicHeader(FileParse* r) {
   Unt totalLen = 0;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PUBLIC) {
         totalLen += (r->c[i].c.len + 2); //+2 for the semicolon & newline char
      }
   }
   
   CS newContent = malloc(totalLen + 1);
   newContent[totalLen] = ZERO;
   CS w = newContent;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PUBLIC) {
         memcpy(w, r->c[i].c.c, r->c[i].c.len);
         w += r->c[i].c.len;
         w[0] = ';';
         w[1] = '\n';
         w += 2;
      }
   }
   
   if (w - newContent != totalLen) {
      printf("ERROR in public header: totalLen %d but wrote only %d", totalLen, w - newContent);
   }
   return (Text){newContent, totalLen};
}

private Boole
dirExists(CS path) {
    struct stat statResult;

    // stat() returns 0 on success
    if (stat(path, OUT &statResult) == 0) {
        // Check if the path is a directory
        return S_ISDIR(statResult.st_mode);
    }

    // Path does not exist or is not accessible
    return false;
}

private CS
determinePublicName(FileParse* r, NULLABLE CS subdir) {
   CS publicName;
   if (subdir) {
      Unt subdirLen = strlen(subdir);
      Unt len = r->fn.len + 1 + subdirLen;
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
      memcpy(publicName + r->fn.dirLen + subdirLen + 1, r->fn.c + r->fn.dirLen, r->fn.len - r->fn.dirLen);
      publicName[len - 1] = 'h';
   } else {
      publicName = malloc(r->fn.len + 1);
      publicName[r->fn.len] = ZERO;
      memcpy(publicName, r->fn.c, r->fn.len - 1);
      publicName[r->fn.len - 1] = 'h';
   }
   return publicName;
}

private void
writePublicHeader(FileParse* r, CS publicName) {
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

private CS
buildFileImpl(FileParse* r, Text existingDecls, Unt beforeLen, Unt afterLen, Unt totalLen) {
   CS newContent = malloc(totalLen + 1);
   newContent[totalLen] = ZERO;
   CS w = newContent;
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
         memcpy(w, r->c[i].c.c, r->c[i].c.len);
         w += r->c[i].c.len;
         w[0] = ';';
         w[1] = '\n';
         w += 2;
      }
   }
   
   if (existingDecls.len == 0) {
      memcpy(w, forwDeclEpilogue, sizeof(forwDeclEpilogue) - 1);
      w += sizeof(forwDeclEpilogue);
      w[-1] = '\n';
   }
   memcpy(w, r->source.c + beforeLen + existingDecls.len, afterLen);
   w += afterLen;
   
   if (w - newContent != totalLen) {
      printf("ERROR totalLen %d but wrote only %d", totalLen, w - newContent);
   }
   
   return newContent;
}

//Return new allocated string with file content
private CS
buildFileWithForwDecls(FileParse* r) {
   Text existingDecls = r->existingForwDecls;
   if (existingDecls.len == 0) {
      existingDecls = writeDeterminePlaceForForwDecls(r);
   }
   
   Unt fwDeclLen = 0;
   for (Unt i = 0; i < r->len; i++) {
      if (r->c[i].acc == PRIVATE && r->c[i].kind == FUNCTION) {
         fwDeclLen += (r->c[i].c.len + 2); //2 = 1 for the semicolon + 1 for newline char
      }
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
   
   return buildFileImpl(r, existingDecls, beforeLen, afterLen, totalLen);
}

private void
writeForwDecl(FileParse* r) {
   CS newContent = buildFileWithForwDecls(r);
   
   FILE* out = fopen(r->fn.c, "w");
   fputs(newContent, out);
   fclose(out);
   free(newContent);
}

private void 
writeResults(FileParse* r, NULLABLE CS subdir) {
   Unt countPublics = 0;
   Unt countPrivateFns = 0; //functions only, only they need forward declarations
   Unt countInternals = 0;
   for (int i = 0; i < r->len; i++) {
      ToplevelThing thing = r->c[i];
      
      switch(thing.acc) {
      case PUBLIC: countPublics++; break;
      case PRIVATE: 
         if (thing.kind == FUNCTION)
            countPrivateFns++; 
         break;
      case INTERNAL: countInternals++; break;
      }
      
      //fwrite(thing.c.c, 1, thing.c.len, stdout);
      //printf("\n");
   }
   if (countPublics > 0) {
      CS publicName = determinePublicName(r, subdir);
      writePublicHeader(r, publicName);
   }
   if (countPrivateFns > 0) {
      //Need to rewrite the source file (.c) to add/update the forward fn declarations
      //printf("%s\n", buildFileWithForwDecls(r));
      writeForwDecl(r);
   }
}

//}}}
//{{{config file



//}}}

private void
printUsage() {
      printf("Example usage:\n");
      printf("betterc source/file.c\n");
      printf("betterc -d headers source/file.c\n");
      printf("\n");
}

privateComp typedef struct {
   CS fn;
   NULLABLE CS subdir;
   Boole correct;
} CommLine;

//Return subdir if it's specified, or null
private CommLine
parseCommLine(int argc, char** argv) {
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

pub int
main(int argc, char** argv) {
   CommLine commLine = parseCommLine(argc, argv);
   if (!commLine.correct) { 
      printUsage();
      return 1;
   } 
   
   FilePath inpFile = filePath(commLine.fn);
   FileParse parseRes = processSourceFile(inpFile); 
   //printf("Parsed %d toplevels\n", parseRes.len);
   writeResults(&parseRes, commLine.subdir);
}

