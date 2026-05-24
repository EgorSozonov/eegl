#include "wren.h"
#include <stdio.h>

#define private static
#define null NULL

void 
writeFn(WrenVM* vm, CS text) {
   printf("%s", text);
}

private CS //:readSourceFile
readSourceFile(CS fName) {
   FILE *file = fopen(fName, "r");
   if (!file)
      { return null; }

   // Go to the end of the file
   if (fseek(file, 0L, SEEK_END) != 0)
      { goto cleanup; }
   long fileSize = ftell(file);
   if (fileSize == -1)
      { goto cleanup; }
   // Allocate our buffer to that size, with space for the standard text in front of it
   CS result = malloc(fileSize + 1);

   // Go back to the start of the file
   if (fseek(file, 0L, SEEK_SET) != 0)
      { goto cleanup; }

   // Read the entire file into memory
   size_t lenSource = fread(result , 1, fileSize, file);

   Int const len = lenSource; // extra 1 for the '\0'
   if (ferror(file) != 0 ) {
      fputs("Error reading file", stderr);
   } else {
      result[len] = '\0'; // Just to be safe
   }
   cleanup:
   fclose(file);
   return result;
}

int 
main(int argc, char** argv) {
   WrenConfiguration config;
   wrenInitConfiguration(&config);
   config.writeFn = &writeFn;
   WrenVM* vm = wrenNewVM(&config);
   
   CS module = "main";
   CS script = readSourceFile("hw.wren");
   

   WrenInterpretResult result = wrenInterpret(vm, module, script);

   switch (result) {
   case WREN_RESULT_COMPILE_ERROR:
     { printf("Compile Error!\n"); } break;
   case WREN_RESULT_RUNTIME_ERROR:
     { printf("Runtime Error!\n"); } break;
   case WREN_RESULT_SUCCESS:
     { printf("Success!\n"); } break;
   }
   
   //Calling methods from C via handles
   WrenHandle* hndl = wrenMakeCallHandle(vm, "meth/2");
   wrenEnsureSlots(vm, 3);
   wrenGetVariable(vm, "main", "Foo", 0);
   wrenSetSlotDouble(vm, 1, 10.0);
   wrenSetSlotDouble(vm, 2, 2.0);
   
   WrenInterpretResult res = wrenCall(vm, hndl);
   if (res == WREN_RESULT_SUCCESS) {
      double numericResult = wrenGetSlotDouble(vm, 0);
      printf("%f\n", numericResult);
   } else {
      printf("some error\n");
   }
 
   wrenFreeVM(vm);
   
   printf("Hwllo world\n");
}
