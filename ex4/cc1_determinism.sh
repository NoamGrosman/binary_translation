#!/bin/bash
# Is cc1's profile deterministic run-to-run under the SAME tool?
cd ~/binary_translation/ex4 || exit 1
ulimit -c 0
PIN=/home/noam/pin/pin

$PIN -t src/obj-intel64/bprofile_orig.so -prof_time 9999 -- ./cc1 200.i -o /tmp/cc1_d1.s > /dev/null 2>&1
cp edge-profile.csv cc1_ORIG_A.csv
$PIN -t src/obj-intel64/bprofile_orig.so -prof_time 9999 -- ./cc1 200.i -o /tmp/cc1_d2.s > /dev/null 2>&1
cp edge-profile.csv cc1_ORIG_B.csv

if cmp -s cc1_ORIG_A.csv cc1_ORIG_B.csv; then
  echo "SAME_TOOL_TWICE: IDENTICAL -> the opt-vs-orig diff is REAL, investigate"
else
  echo "SAME_TOOL_TWICE: DIFFER -> cc1 is nondeterministic; opt-vs-orig diff is expected"
  diff cc1_ORIG_A.csv cc1_ORIG_B.csv | head -6
  echo "total differing lines: $(diff cc1_ORIG_A.csv cc1_ORIG_B.csv | grep -c '^[<>]')  of $(wc -l < cc1_ORIG_A.csv)"
fi
