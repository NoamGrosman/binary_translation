#!/bin/bash
# Post-brk-fix validation: 6 sgcc runs per tool + layout print + 1 bzip2 run each.
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

run_cfg() {
  local label="$1"; shift
  local tool="$1"; shift
  local p=0 f=0
  for i in 1 2 3 4 5 6; do
    rm -f 200.s
    $PIN -t $tool "$@" -prof_time 3 -- ./sgcc_base.mytest-m64 200.i -o 200.s > /tmp/mx2.log 2>&1
    if [ $? -eq 0 ] && cmp -s 200.s native_200.s; then p=$((p+1)); else
      f=$((f+1))
      echo "  $label run$i FAIL phase(disabling=$(grep -c disabling /tmp/mx2.log))"
    fi
  done
  grep -m1 'region layout\|allocated memory' /tmp/mx2.log
  echo "$label: pass=$p fail=$f"
}

run_cfg OPT_FULL src/obj-intel64/bprofile.so
run_cfg ORIG     src/obj-intel64/bprofile_orig.so

echo "=== bzip2 sanity ==="
for tool in bprofile.so bprofile_orig.so; do
  rm -f input-long.txt.bz2
  $PIN -t src/obj-intel64/$tool -prof_time 2 -- ./bzip2 -k -f input-long.txt > /tmp/bz.log 2>&1
  ec=$?
  cmp -s input-long.txt.bz2 native_input-long.txt.bz2 && echo "$tool bzip2 PASS" || echo "$tool bzip2 FAIL ec=$ec"
done
