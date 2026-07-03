#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

echo "=== optimized tool on sgcc (prof_time 5, exercises disable) ==="
rm -f 200.s
$PIN -t src/obj-intel64/bprofile.so -prof_time 5 -- ./sgcc_base.mytest-m64 200.i -o 200.s > opt_sgcc.log 2>&1
echo "EXIT=$?"
cmp 200.s native_200.s && echo "200S_IDENTICAL_OPT"
grep -E 'dead-reg|disabling|unable to set' opt_sgcc.log
mv edge-profile.csv edge-profile.sgcc.opt.csv
head -3 edge-profile.sgcc.opt.csv
wc -l edge-profile.sgcc.opt.csv

echo "=== optimized tool on bzip2 (prof_time 2, exercises disable) ==="
rm -f input-long.txt.bz2
$PIN -t src/obj-intel64/bprofile.so -prof_time 2 -- ./bzip2 -k -f input-long.txt > opt_bzip2_short.log 2>&1
echo "EXIT=$?"
cmp input-long.txt.bz2 native_input-long.txt.bz2 && echo "BZ2_IDENTICAL_OPT_SHORT"
grep -E 'disabling|unable to set|Translated code run' opt_bzip2_short.log
