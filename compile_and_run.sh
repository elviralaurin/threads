#!/usr/bin/bash
gcc -c test.c green.c
gcc -o test.out test.o green.o
./test.out
