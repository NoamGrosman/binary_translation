#!/bin/bash
# V1: clean rebuild of both tools.
cd ~/binary_translation/ex4/src || exit 1
rm -rf obj-intel64
make PIN_ROOT=/home/noam/pin obj-intel64/bprofile.so obj-intel64/bprofile_orig.so 2> build.log
rc=$?
grep -i "error\|warning" build.log | head -20
if [ $rc -eq 0 ] && [ -f obj-intel64/bprofile.so ] && [ -f obj-intel64/bprofile_orig.so ]; then
  echo "BUILD_OK"
else
  echo "BUILD_FAILED rc=$rc"
  tail -30 build.log
fi
