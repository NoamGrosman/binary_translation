#!/bin/bash
# Gather remaining REDAME data: sgcc_base opt stub stats + ex2 line recount.
cd ~/binary_translation/ex4
PIN=/home/noam/pin/pin

echo "=== sgcc_base opt-tool stats (also one more correctness pass) ==="
rm -f 200.s
$PIN -t src/obj-intel64/bprofile.so -prof_time 10 -- ./sgcc_base.mytest-m64 200.i -o 200.s 2> gather_sgcc.log
echo "exit: $?"
grep -E "dead-reg optimization|TCVERIFY" gather_sgcc.log
if cmp -s native_200.s 200.s; then echo "SGCC_BYTE_IDENTICAL"; else echo "SGCC_DIFFERS"; fi

echo "=== ex2 reference comparison (Task 4 recount) ==="
wc -l bzip_OPT.csv ex2ref/edge-profile.csv
echo -n "byte-identical lines: "
comm -12 <(sort bzip_OPT.csv) <(sort ex2ref/edge-profile.csv) | wc -l
echo "sample identical lines:"
comm -12 <(sort bzip_OPT.csv) <(sort ex2ref/edge-profile.csv) | head -3
