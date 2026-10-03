#! /usr/bin/bash
tar -c dev/betterc.c src/*.c src/*.h makefile -f eegl-$(date +"%Y%m%d").tar

