#!/bin/bash
cd ~/binary_translation/ex4
cp -n ~/binary_translation/ex3/sgcc_base.mytest-m64 . 2>/dev/null
chmod +x sgcc_base.mytest-m64
grep -c avx512 <(objdump -d sgcc_base.mytest-m64 2>/dev/null | grep -oE 'kmov|vpternlog' | head -5) 2>/dev/null
echo "--- native sgcc run ---"
ulimit -c 0
/usr/bin/time -v ./sgcc_base.mytest-m64 200.i -o 200.s > native_sgcc.log 2>&1
ec=$?
echo "EXIT_CODE=$ec"
ls -la 200.s && md5sum 200.s
tail -3 native_sgcc.log
