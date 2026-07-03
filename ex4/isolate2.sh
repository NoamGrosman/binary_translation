#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
T=src/obj-intel64/bprofile.so
for cfg in "-no_mem_add" "-no_skip_dead"; do
  for i in 1 2 3; do
    rm -f 200.s
    $PIN -t $T $cfg -prof_time 3 -- ./sgcc_base.mytest-m64 200.i -o 200.s > /tmp/iso2.log 2>&1
    ec=$?
    if [ $ec -eq 0 ] && cmp -s 200.s native_200.s; then
      echo "$cfg run$i PASS"
    else
      echo "$cfg run$i FAIL ec=$ec phase: $(grep -c disabling /tmp/iso2.log)"
    fi
  done
done
