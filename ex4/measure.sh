#!/bin/bash
# Task 5 A/B measurement: orig vs optimized tool, /usr/bin/time -v, N runs each.
# Usage: measure.sh <runs> <prof_time>
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
RUNS=${1:-5}
PROF=${2:-10}
OUT=measure_results.txt
: > $OUT

run_one() { # tool label binary...
  local tool=$1 label=$2; shift 2
  for i in $(seq 1 $RUNS); do
    /usr/bin/time -v $PIN -t $tool -prof_time $PROF -- "$@" > /dev/null 2> /tmp/time_out.txt
    local wall=$(grep 'Elapsed (wall clock)' /tmp/time_out.txt | awk '{print $NF}')
    local user=$(grep 'User time' /tmp/time_out.txt | awk '{print $NF}')
    local sys=$(grep 'System time' /tmp/time_out.txt | awk '{print $NF}')
    local minf=$(grep 'Minor.*page faults' /tmp/time_out.txt | awk '{print $NF}')
    local majf=$(grep 'Major.*page faults' /tmp/time_out.txt | awk '{print $NF}')
    echo "$label run$i wall=$wall user=$user sys=$sys minflt=$minf majflt=$majf" | tee -a $OUT
  done
}

case "$3" in
  sgcc)
    run_one src/obj-intel64/bprofile_orig.so ORIG_SGCC ./sgcc_base.mytest-m64 200.i -o /tmp/m200.s
    run_one src/obj-intel64/bprofile.so      OPT_SGCC  ./sgcc_base.mytest-m64 200.i -o /tmp/m200.s
    ;;
  *)
    run_one src/obj-intel64/bprofile_orig.so ORIG_BZIP ./bzip2 -k -f input-long.txt
    run_one src/obj-intel64/bprofile.so      OPT_BZIP  ./bzip2 -k -f input-long.txt
    ;;
esac
