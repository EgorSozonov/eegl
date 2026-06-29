//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## main.c: the entrypoint of Eegl

#define EXTERN // this makes all the global vars defined here, see src/eegl.h

#include "eegl.h"

int 
main(int argc, char** argv) {
   return appMain(argc, argv); // see src/motor.c
}

