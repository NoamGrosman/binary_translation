#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
T=src/obj-intel64/bprofile.so

run() {
  local label="$1"; shift
  rm -f 200.s
  $PIN -t $T "$@" -prof_time 5 -- ./sgcc_base.mytest-m64 200.i -o 200.s > iso_$label.log 2>&1
  local ec=$?
  if [ $ec -eq 0 ] && cmp -s 200.s native_200.s; then
    echo "$label: PASS"
  else
    echo "$label: FAIL (exit=$ec)"
  fi
}

run only_skip -no_mem_add
run only_memadd -no_skip_dead
