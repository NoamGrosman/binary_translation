#!/bin/bash
# Full-run profiling (prof_time larger than the run) on bzip2 with both tools;
# counts must match exactly since bzip2 is deterministic.
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

echo "=== optimized tool ==="
$PIN -t src/obj-intel64/bprofile.so -prof_time 9999 -- ./bzip2 -k -f input-long.txt > opt.log 2>&1
echo "EXIT=$?"
cmp input-long.txt.bz2 native_input-long.txt.bz2 && echo "BZ2_IDENTICAL_OPT"
grep 'dead-reg' opt.log
mv edge-profile.csv edge-profile.opt.csv

echo "=== orig tool ==="
$PIN -t src/obj-intel64/bprofile_orig.so -prof_time 9999 -- ./bzip2 -k -f input-long.txt > orig.log 2>&1
echo "EXIT=$?"
cmp input-long.txt.bz2 native_input-long.txt.bz2 && echo "BZ2_IDENTICAL_ORIG"
mv edge-profile.csv edge-profile.orig.csv

echo "=== count comparison ==="
if cmp -s edge-profile.opt.csv edge-profile.orig.csv; then
  echo "PROFILES_IDENTICAL"
else
  echo "PROFILES_DIFFER:"
  diff edge-profile.opt.csv edge-profile.orig.csv | head -20
fi
wc -l edge-profile.opt.csv edge-profile.orig.csv
head -5 edge-profile.opt.csv
