#!/bin/bash
cd ~/binary_translation/ex4
mv 200.s native_200.s
ulimit -c 0
/home/noam/pin/pin -t src/obj-intel64/bprofile_orig.so -prof_time 5 -- ./sgcc_base.mytest-m64 200.i -o 200.s > pin_sgcc.log 2>&1
ec=$?
echo "EXIT_CODE=$ec"
ls -la 200.s 2>/dev/null && md5sum 200.s native_200.s
tail -8 pin_sgcc.log
