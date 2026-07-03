#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
for i in 1 2 3 4; do
  rm -f 200.s
  $PIN -t src/obj-intel64/bprofile.so -prof_time 3 -- ./sgcc_base.mytest-m64 200.i -o 200.s > stab_opt_$i.log 2>&1
  ec=$?
  if [ $ec -eq 0 ] && cmp -s 200.s native_200.s; then
    echo "run$i PASS"
  else
    echo "run$i FAIL ec=$ec phase: $(grep -c 'disabling' stab_opt_$i.log) $(grep -c 'gathering disabled' stab_opt_$i.log)"
    dmesg 2>/dev/null | grep -E 'segfault|trap' | tail -1
  fi
done
