#!/bin/bash
# Repeated sgcc runs with both tools to verify the disable race is gone.
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
pass=0; fail=0
for tool in bprofile.so bprofile_orig.so; do
  for i in 1 2 3; do
    rm -f 200.s
    $PIN -t src/obj-intel64/$tool -prof_time 3 -- ./sgcc_base.mytest-m64 200.i -o 200.s > /tmp/stab.log 2>&1
    ec=$?
    if [ $ec -eq 0 ] && cmp -s 200.s native_200.s; then
      echo "$tool run$i PASS"; pass=$((pass+1))
    else
      echo "$tool run$i FAIL ec=$ec"; fail=$((fail+1))
      grep -E 'disabling|could not stop' /tmp/stab.log | head -2
    fi
  done
done
echo "TOTAL pass=$pass fail=$fail"
