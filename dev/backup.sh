#! /usr/bin/bash
tar -C $1 -c dev/betterc.c $1/dev/indexGenerator.c $1/src/*.c $1/src/*.h makefile \
   -f /tmp/eegl.tar 2>/dev/null

