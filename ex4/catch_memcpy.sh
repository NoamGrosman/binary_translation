#!/bin/bash
# Run opt-full + catch_segv in a separate dir until it crashes; print report.
mkdir -p ~/binary_translation/ex4/dbg
cd ~/binary_translation/ex4/dbg
ln -sf ../sgcc_base.mytest-m64 .
ln -sf ../200.i .
ulimit -c 0
PIN=/home/noam/pin/pin
for i in 1 2 3 4 5 6 7 8; do
  rm -f d200.s
  $PIN -t ../src/obj-intel64/bprofile.so -catch_segv -prof_time 3 -- ./sgcc_base.mytest-m64 200.i -o d200.s > run.log 2>&1
  ec=$?
  if grep -q SEGVREPORT run.log; then
    echo "CRASH on run $i:"
    grep -A24 SEGVREPORT run.log
    exit 0
  fi
  echo "run $i clean (ec=$ec)"
done
echo "no crash in 8 runs"
