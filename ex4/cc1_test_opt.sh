#!/bin/bash
# cc1 under the OPTIMIZED tool: 6 runs, byte-compare each against native.
cd ~/binary_translation/ex4 || exit 1
ulimit -c 0
PIN=/home/noam/pin/pin
pass=0; fail=0
for i in 1 2 3 4 5 6; do
  rm -f cc1_200.s
  $PIN -t src/obj-intel64/bprofile.so -prof_time 10 -- ./cc1 200.i -o cc1_200.s > /dev/null 2> cc1_opt_run$i.log
  rc=$?
  if [ $rc -eq 0 ] && cmp -s native_cc1_200.s cc1_200.s; then
    echo "OPT run$i: PASS  $(grep -o 'TCVERIFY: [0-9]* bad' cc1_opt_run$i.log)"
    pass=$((pass+1))
  else
    echo "OPT run$i: FAIL (exit=$rc)"; tail -4 cc1_opt_run$i.log
    fail=$((fail+1))
  fi
done
grep -m1 "dead-reg optimization" cc1_opt_run1.log
echo "CC1_OPT pass=$pass fail=$fail"
