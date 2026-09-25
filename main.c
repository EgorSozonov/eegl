//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## main.c: the entrypoint of Eegl

#define EXTERN // this makes all the global vars be defined here, see src/eegl.h
#define MAIN_C // more initialization of globals, see src/eegl.h

#include "src/eegl.h"
#include "src/h/motor.types.h"
#include "src/h/motor.h"

int 
main(int argc, char** argv) {
   return appMain(argc, argv); // see src/motor.c
}

