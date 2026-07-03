#!/bin/bash
# Quick smoke: bzip2 once + sgcc_peak once with the FIXED optimized tool.
cd ~/binary_translation/ex4 || exit 1
PIN=/home/noam/pin/pin
T=src/obj-intel64/bprofile.so

echo "=== bzip2 (opt tool) ==="
rm -f input-long.txt.bz2
$PIN -t $T -prof_time 9999 -- ./bzip2 -k -f input-long.txt 2> smoke_bzip.log
echo "exit: $?"
grep -E "TCVERIFY|dead-reg optimization" smoke_bzip.log
if cmp -s native_input-long.txt.bz2 input-long.txt.bz2; then echo "BZ2_BYTE_IDENTICAL"; else echo "BZ2_DIFFERS"; fi

echo "=== sgcc_peak (opt tool, the V0 failing case) ==="
rm -f 200_peak.s
$PIN -t $T -prof_time 10 -- ./sgcc_peak.mytest-m64 200.i -o 200_peak.s 2> smoke_peak.log
echo "exit: $?"
grep -E "TCVERIFY|dead-reg optimization" smoke_peak.log
if cmp -s native_peak_200.s 200_peak.s; then echo "PEAK_BYTE_IDENTICAL"; else echo "PEAK_DIFFERS"; fi

echo "=== CSV quick checks ==="
head -3 edge-profile.csv
awk -F', ' '{ if (NF < 4) { print "LINE_UNDER_4_FIELDS: " $0; bad=1 } } END { if (!bad) print "ALL_LINES_HAVE_4PLUS_FIELDS" }' edge-profile.csv
awk -F', ' '{ if ($3 + $4 != $2) { print "TAKEN_FT_MISMATCH: " $0; bad=1 } } END { if (!bad) print "TAKEN_PLUS_FT_OK" }' edge-profile.csv
