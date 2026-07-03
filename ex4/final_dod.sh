#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

echo "== 1. clean rebuild =="
cd src
rm -rf obj-intel64
make PIN_ROOT=/home/noam/pin obj-intel64/bprofile.so obj-intel64/bprofile_orig.so > /tmp/build.log 2>&1 \
  && echo "BUILD_OK" || { echo BUILD_FAILED; grep -i error /tmp/build.log | head; exit 1; }
cd ..
cp -f src/obj-intel64/bprofile.so .

echo "== 2. bzip2 final run =="
rm -f input-long.txt.bz2 edge-profile.csv
$PIN -t bprofile.so -prof_time 2 -- ./bzip2 -k -f input-long.txt > /tmp/fin_bz.log 2>&1
echo "exit=$?"
cmp input-long.txt.bz2 native_input-long.txt.bz2 && echo "BZ2_BYTE_IDENTICAL"

echo "== 4. edge-profile.csv checks =="
ls -la edge-profile.csv
head -3 edge-profile.csv
# sorted hottest->coldest?
awk -F', ' '{print $2}' edge-profile.csv | sort -rn -c && echo "SORTED_DESC_OK"
# taken+fallthru==exec on all 4-field non-hex-3rd lines
awk -F', ' 'NF==4 && $3 !~ /^0x/ { if ($3+$4 != $2) { print "BAD:" $0; bad=1 } } END { if (!bad) print "TAKEN_PLUS_FT_OK" }' edge-profile.csv

echo "== 3. sgcc final run =="
rm -f 200.s
$PIN -t bprofile.so -prof_time 5 -- ./sgcc_base.mytest-m64 200.i -o 200.s > /tmp/fin_gcc.log 2>&1
echo "exit=$?"
cmp 200.s native_200.s && echo "SGCC_BYTE_IDENTICAL"
grep -E 'TCVERIFY|dead-reg' /tmp/fin_gcc.log

echo "== 8. zip contents =="
unzip -l ex4.zip | tail -10
