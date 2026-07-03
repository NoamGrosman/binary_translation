#!/bin/bash
# Task 5 A/B measurement, interleaved (ORIG/OPT alternating per iteration)
# so environmental drift hits both tools equally.
# Usage: measure_interleaved.sh <runs> <prof_time> <bzip|sgcc>
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin
RUNS=${1:-5}
PROF=${2:-10}
TARGET=${3:-bzip}
OUT=measure_${TARGET}.txt
: > $OUT

run_one() { # tool label binary...
  local tool=$1 label=$2; shift 2
  /usr/bin/time -v $PIN -t $tool -prof_time $PROF -- "$@" > /dev/null 2> /tmp/time_out.txt
  local wall=$(grep 'Elapsed (wall clock)' /tmp/time_out.txt | awk '{print $NF}')
  local user=$(grep 'User time' /tmp/time_out.txt | awk '{print $NF}')
  local sys=$(grep 'System time' /tmp/time_out.txt | awk '{print $NF}')
  local minf=$(grep 'Minor.*page faults' /tmp/time_out.txt | awk '{print $NF}')
  echo "$label wall=$wall user=$user sys=$sys minflt=$minf" | tee -a $OUT
}

for i in $(seq 1 $RUNS); do
  if [ "$TARGET" = sgcc ]; then
    run_one src/obj-intel64/bprofile_orig.so ORIG_run$i ./sgcc_base.mytest-m64 200.i -o /tmp/m200.s
    run_one src/obj-intel64/bprofile.so      OPT_run$i  ./sgcc_base.mytest-m64 200.i -o /tmp/m200.s
  else
    run_one src/obj-intel64/bprofile_orig.so ORIG_run$i ./bzip2 -k -f input-long.txt
    run_one src/obj-intel64/bprofile.so      OPT_run$i  ./bzip2 -k -f input-long.txt
  fi
done
echo "MEASURE_${TARGET}_DONE"
