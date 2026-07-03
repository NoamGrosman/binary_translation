#!/bin/bash
# V0: reproduce the flags-across-indirect bug on sgcc_peak with the CURRENT tool.
cd ~/binary_translation/ex4 || exit 1

if [ ! -f sgcc_peak.mytest-m64 ]; then
  cp ../ex3/sgcc_peak.mytest-m64 . && chmod +x sgcc_peak.mytest-m64
fi

# Native reference (once).
if [ ! -f native_peak_200.s ]; then
  echo "=== native sgcc_peak run ==="
  ./sgcc_peak.mytest-m64 200.i -o native_peak_200.s
  echo "native exit: $?"
fi
ls -la native_peak_200.s

echo "=== pin run with CURRENT bprofile.so ==="
rm -f 200_peak.s
/home/noam/pin/pin -t src/obj-intel64/bprofile.so -prof_time 10 -- ./sgcc_peak.mytest-m64 200.i -o 200_peak.s 2> v0_peak.log
echo "pin run exit: $?"
tail -5 v0_peak.log

if cmp -s native_peak_200.s 200_peak.s; then
  echo "V0_RESULT: PEAK_BYTE_IDENTICAL (bug did not manifest this run)"
else
  echo "V0_RESULT: PEAK_OUTPUT_DIFFERS (bug reproduced)"
  ls -la native_peak_200.s 200_peak.s
fi
