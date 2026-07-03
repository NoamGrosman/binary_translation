#!/bin/bash
cd ~/binary_translation/ex4
TOOL=${1:-src/obj-intel64/bprofile.so}
PROF=${2:-2}
ulimit -c 0
/home/noam/pin/pin -t $TOOL -prof_time $PROF -- ./bzip2 -k -f input-long.txt > pin_bzip2.log 2>&1
ec=$?
echo "EXIT_CODE=$ec"
cmp input-long.txt.bz2 native_input-long.txt.bz2 && echo "OUTPUT_IDENTICAL"
tail -4 pin_bzip2.log
