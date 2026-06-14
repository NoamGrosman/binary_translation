#!/bin/bash
# Binary search for the first translated routine whose translation breaks the app.
# "good" = exit 132 (SIGILL on the AVX-512 instr, unavoidable on this CPU)
# "bad"  = exit 139 (SIGSEGV in ld.so caused by a broken translation)
cd /home/noam/binary_translation/ex3

run() {
  timeout 300 /home/noam/pin/pin -t src/obj-intel64/btranslate.so -max_rtns "$1" \
      -- ./cpugcc_r_base.Oz-m64 200.i -o tmp_bisect.s > /dev/null 2> bisect.log
  echo $?
}

a=$(run 1)
b=$(run 14956)
echo "endpoint N=1 -> rc=$a ; N=14956 -> rc=$b"
if [ "$a" != "132" ] || [ "$b" != "139" ]; then
  echo "ENDPOINTS DO NOT MATCH EXPECTED GOOD/BAD PATTERN - aborting"
  exit 1
fi

lo=1   # good
hi=14956  # bad
while [ $((hi - lo)) -gt 1 ]; do
  mid=$(( (lo + hi) / 2 ))
  rc=$(run $mid)
  echo "N=$mid -> rc=$rc"
  if [ "$rc" = "139" ]; then
    hi=$mid
    cp bisect.log bisect_bad.log
  else
    lo=$mid
  fi
done

echo "FIRST BAD N = $hi"
echo "culprit routine:"
grep "translating rtn #$hi:" bisect_bad.log
