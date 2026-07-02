#!/bin/bash
cd /home/noam/binary_translation/ex3
BIN=./sgcc_peak.mytest-m64
run() {
  timeout 180 /home/noam/pin/pin -t ex3.so -max_rtns "$1" -- $BIN 200.i -o /tmp/bp.s >/dev/null 2>/tmp/bp.err
  echo $?
}
LO=1; HI=13362
a=$(run $LO); b=$(run $HI)
echo "endpoint N=$LO -> rc=$a ; N=$HI -> rc=$b"
if [ "$a" != "0" ] || [ "$b" = "0" ]; then echo "ENDPOINTS bad: a=$a b=$b"; exit 1; fi
lo=$LO; hi=$HI
while [ $((hi-lo)) -gt 1 ]; do
  mid=$(((lo+hi)/2)); rc=$(run $mid); echo "N=$mid -> rc=$rc"
  if [ "$rc" = "0" ]; then lo=$mid; else hi=$mid; cp /tmp/bp.err /tmp/bp_bad.err; fi
done
echo "FIRST BAD N=$hi"; grep "translating rtn #$hi:" /tmp/bp_bad.err
