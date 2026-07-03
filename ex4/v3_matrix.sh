#!/bin/bash
# V2-V6: full correctness matrix with the fixed tools.
cd ~/binary_translation/ex4 || exit 1
PIN=/home/noam/pin/pin
OPT=src/obj-intel64/bprofile.so
ORIG=src/obj-intel64/bprofile_orig.so

echo "===== V5: PIE test ====="
gcc -O2 -o pie_switch pie_switch.c || { echo "PIE_BUILD_FAILED"; exit 1; }
file pie_switch | grep -o "pie executable" || readelf -h pie_switch | grep Type
./pie_switch > pie_native.out
echo "native exit: $? out: $(cat pie_native.out)"
$PIN -t $OPT -prof_time 9999 -- ./pie_switch > pie_pin.out 2> pie_pin.log
echo "pin exit: $?"
grep -E "TCVERIFY|dead-reg optimization|Invalid rip displacement" pie_pin.log
if cmp -s pie_native.out pie_pin.out; then echo "PIE_OUTPUT_IDENTICAL"; else echo "PIE_OUTPUT_DIFFERS: native=$(cat pie_native.out) pin=$(cat pie_pin.out)"; fi
# The dispatch BBL must appear in the profile with indirect targets (>=6 fields).
awk -F', ' 'NF >= 6 { print "PIE_INDIRECT_LINE: " $0 }' edge-profile.csv | head -3
cp edge-profile.csv pie_edge.csv

echo "===== V2: bzip2 3x per tool ====="
for tool in OPT ORIG; do
  T=$([ $tool = OPT ] && echo $OPT || echo $ORIG)
  pass=0
  for i in 1 2 3; do
    rm -f input-long.txt.bz2
    $PIN -t $T -prof_time 9999 -- ./bzip2 -k -f input-long.txt 2> /dev/null
    rc=$?
    if [ $rc -eq 0 ] && cmp -s native_input-long.txt.bz2 input-long.txt.bz2; then
      pass=$((pass+1))
    else
      echo "BZIP_$tool run $i FAILED rc=$rc"
    fi
    # Keep one full-run CSV per tool for the V6 counts-identity check.
    [ $i -eq 1 ] && cp edge-profile.csv bzip_${tool}.csv
  done
  echo "BZIP_$tool pass=$pass/3"
done

echo "===== V6: counts identity (bzip2, prof_time 9999) ====="
if cmp -s bzip_OPT.csv bzip_ORIG.csv; then
  echo "PROFILES_IDENTICAL ($(wc -l < bzip_OPT.csv) lines)"
else
  echo "PROFILES_DIFFER"
  diff bzip_OPT.csv bzip_ORIG.csv | head -10
fi

echo "===== V3: sgcc_base 6x per tool ====="
for tool in OPT ORIG; do
  T=$([ $tool = OPT ] && echo $OPT || echo $ORIG)
  pass=0; fail=0
  for i in 1 2 3 4 5 6; do
    rm -f 200.s
    $PIN -t $T -prof_time 10 -- ./sgcc_base.mytest-m64 200.i -o 200.s 2> sgcc_last.log
    rc=$?
    if [ $rc -eq 0 ] && cmp -s native_200.s 200.s; then
      pass=$((pass+1))
    else
      fail=$((fail+1)); echo "SGCC_$tool run $i FAILED rc=$rc"; tail -3 sgcc_last.log
    fi
  done
  echo "SGCC_BASE_$tool pass=$pass fail=$fail"
done

echo "===== V4: sgcc_peak 6x (opt tool) ====="
pass=0; fail=0
for i in 1 2 3 4 5 6; do
  rm -f 200_peak.s
  $PIN -t $OPT -prof_time 10 -- ./sgcc_peak.mytest-m64 200.i -o 200_peak.s 2> peak_last.log
  rc=$?
  tcv=$(grep -c "TCVERIFY: 0 bad" peak_last.log)
  if [ $rc -eq 0 ] && [ "$tcv" = "1" ] && cmp -s native_peak_200.s 200_peak.s; then
    pass=$((pass+1))
  else
    fail=$((fail+1)); echo "PEAK run $i FAILED rc=$rc tcv=$tcv"; tail -3 peak_last.log
  fi
done
echo "SGCC_PEAK_OPT pass=$pass fail=$fail"

echo "===== V7: CSV format checks (bzip_OPT.csv) ====="
awk -F', ' '{ if (NF < 4) { print "LINE_UNDER_4_FIELDS: " $0; bad=1 } } END { if (!bad) print "ALL_LINES_HAVE_4PLUS_FIELDS" }' bzip_OPT.csv
awk -F', ' '{ if ($3 + $4 != $2) { print "TAKEN_FT_MISMATCH: " $0; bad=1 } } END { if (!bad) print "TAKEN_PLUS_FT_OK" }' bzip_OPT.csv
awk -F', ' 'NR>1 { if ($2 > prev) { print "SORT_VIOLATION line " NR; bad=1 } } { prev=$2 } END { if (!bad) print "SORTED_DESC_OK" }' bzip_OPT.csv

echo "MATRIX_DONE"
