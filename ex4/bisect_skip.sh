#!/bin/bash
# Binary-search the smallest K such that optimizing stubs [0,K) makes sgcc fail.
# Resumable: bounds persist in bisect_state.txt (lo hi).
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
T=src/obj-intel64/bprofile.so
STATE=bisect_state.txt

[ -f $STATE ] || echo "0 188311" > $STATE
read lo hi < $STATE

test_range() { # $1 = hi bound; returns 0 if PASS
  rm -f 200.s
  $PIN -t $T -no_mem_add -skip_hi $1 -prof_time 5 -- ./sgcc_base.mytest-m64 200.i -o 200.s > /tmp/bisect_run.log 2>&1
  local ec=$?
  if [ $ec -eq 0 ] && cmp -s 200.s native_200.s; then return 0; else return 1; fi
}

while [ $((hi - lo)) -gt 1 ]; do
  mid=$(( (lo + hi) / 2 ))
  if test_range $mid; then
    echo "K=$mid PASS"
    lo=$mid
  else
    echo "K=$mid FAIL"
    hi=$mid
  fi
  echo "$lo $hi" > $STATE
done
echo "CULPRIT_STUB_ORDINAL=$lo (prefix [0,$hi) fails, [0,$lo) passes)"
