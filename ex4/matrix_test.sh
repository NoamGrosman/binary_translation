#!/bin/bash
# Quantitative discrimination: 6 runs per config on sgcc.
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

run_cfg() {
  local label="$1"; shift
  local tool="$1"; shift
  local p=0 f=0
  for i in 1 2 3 4 5 6; do
    rm -f 200.s
    $PIN -t $tool "$@" -prof_time 3 -- ./sgcc_base.mytest-m64 200.i -o 200.s > /tmp/mx.log 2>&1
    if [ $? -eq 0 ] && cmp -s 200.s native_200.s; then p=$((p+1)); else
      f=$((f+1))
      echo "  $label run$i FAIL phase(disabling=$(grep -c disabling /tmp/mx.log) applied=$(grep -c 'gathering disabled' /tmp/mx.log))"
    fi
  done
  echo "$label: pass=$p fail=$f"
}

run_cfg ORIG        src/obj-intel64/bprofile_orig.so
run_cfg OPT_FULL    src/obj-intel64/bprofile.so
run_cfg OPT_HEAPBBL src/obj-intel64/bprofile.so -heap_bbl
run_cfg OPT_MEMONLY src/obj-intel64/bprofile.so -no_skip_dead
