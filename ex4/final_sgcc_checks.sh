#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

echo "=== orig tool on sgcc ==="
rm -f 200.s
$PIN -t src/obj-intel64/bprofile_orig.so -prof_time 5 -- ./sgcc_base.mytest-m64 200.i -o 200.s > final_orig_sgcc.log 2>&1
echo "EXIT=$?"
cmp 200.s native_200.s && echo "ORIG_SGCC_IDENTICAL"
grep -cE 'disabling' final_orig_sgcc.log
mv edge-profile.csv edge-profile.sgcc.orig.csv 2>/dev/null

echo "=== optimized tool on sgcc, prof_time 2 (disable path) ==="
rm -f 200.s
$PIN -t src/obj-intel64/bprofile.so -prof_time 2 -- ./sgcc_base.mytest-m64 200.i -o 200.s > final_opt_sgcc2.log 2>&1
echo "EXIT=$?"
cmp 200.s native_200.s && echo "OPT_SGCC2_IDENTICAL"
grep -E 'disabling|Translated code run' final_opt_sgcc2.log
mv edge-profile.csv edge-profile.sgcc.opt2.csv 2>/dev/null
head -3 edge-profile.sgcc.opt2.csv
