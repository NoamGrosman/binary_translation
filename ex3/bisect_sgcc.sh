#!/bin/bash
# Isolate the first translated routine whose translation makes the sgcc binary
# crash at runtime.  good = exit 0 (compiled fine) ; bad = nonzero (segv/abort).
cd /home/noam/binary_translation/ex3

run() {
  timeout 180 /home/noam/pin/pin -t ex3.so -max_rtns "$1" \
      -- ./sgcc_base.mytest-m64 200.i -o /tmp/bisect.s > /dev/null 2> /tmp/bisect.err
  echo $?
}

LO=1
HI=13498

a=$(run $LO)
b=$(run $HI)
echo "endpoint N=$LO -> rc=$a ; N=$HI -> rc=$b"
if [ "$a" != "0" ] || [ "$b" = "0" ]; then
  echo "ENDPOINTS DO NOT MATCH good(0)/bad(!=0) - aborting"
  exit 1
fi

lo=$LO   # good
hi=$HI   # bad
while [ $((hi - lo)) -gt 1 ]; do
  mid=$(( (lo + hi) / 2 ))
  rc=$(run $mid)
  echo "N=$mid -> rc=$rc"
  if [ "$rc" = "0" ]; then
    lo=$mid
  else
    hi=$mid
    cp /tmp/bisect.err /tmp/bisect_bad.err
  fi
done

echo "FIRST BAD N = $hi"
echo "culprit routine:"
grep "translating rtn #$hi:" /tmp/bisect_bad.err
