#!/bin/bash
# cc1 under the ORIG (unoptimized) tool: 3 runs + opt-vs-orig counts identity.
cd ~/binary_translation/ex4 || exit 1
ulimit -c 0
PIN=/home/noam/pin/pin
pass=0; fail=0
for i in 1 2 3; do
  rm -f cc1_200.s
  $PIN -t src/obj-intel64/bprofile_orig.so -prof_time 10 -- ./cc1 200.i -o cc1_200.s > /dev/null 2> cc1_orig_run$i.log
  rc=$?
  if [ $rc -eq 0 ] && cmp -s native_cc1_200.s cc1_200.s; then
    echo "ORIG run$i: PASS"
    pass=$((pass+1))
  else
    echo "ORIG run$i: FAIL (exit=$rc)"; tail -4 cc1_orig_run$i.log
    fail=$((fail+1))
  fi
done
echo "CC1_ORIG pass=$pass fail=$fail"

echo "=== counts identity on cc1 (prof_time 9999, deterministic) ==="
$PIN -t src/obj-intel64/bprofile.so -prof_time 9999 -- ./cc1 200.i -o /tmp/cc1_a.s > /dev/null 2>&1
cp edge-profile.csv cc1_OPT.csv
$PIN -t src/obj-intel64/bprofile_orig.so -prof_time 9999 -- ./cc1 200.i -o /tmp/cc1_b.s > /dev/null 2>&1
cp edge-profile.csv cc1_ORIG.csv
if cmp -s cc1_OPT.csv cc1_ORIG.csv; then
  echo "CC1_PROFILES_IDENTICAL ($(wc -l < cc1_OPT.csv) lines)"
else
  echo "CC1_PROFILES_DIFFER"; diff cc1_OPT.csv cc1_ORIG.csv | head -5
fi
awk -F', ' 'NF<4{bad++} END{if(bad)print "BAD_FIELD_LINES="bad; else print "ALL_LINES_HAVE_4PLUS_FIELDS"}' cc1_OPT.csv
