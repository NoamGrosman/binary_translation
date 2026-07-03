#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
timeout 600 ./cpugcc_r_base.Oz-m64 200.i -o 200.s > native_gcc.log 2>&1
ec=$?
echo "EXIT_CODE=$ec"
ls -la 200.s 2>/dev/null
echo "--- last log lines ---"
tail -5 native_gcc.log
echo "--- dmesg tail ---"
dmesg 2>/dev/null | tail -3
