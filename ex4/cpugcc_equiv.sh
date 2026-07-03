#!/bin/bash
# Does the real (PIE, AVX-512) cpugcc behave IDENTICALLY native vs under the tool?
cd ~/binary_translation/ex4
ulimit -c 0
PIN=/home/noam/pin/pin

echo "=== existing smoke-test pin log for cpugcc (tail) ==="
tail -15 cpugcc_pin.log 2>/dev/null || echo "(no cpugcc_pin.log kept)"

echo
echo "=== NATIVE cpugcc run ==="
rm -f native_cpugcc_200.s
./cpugcc_r_base.Oz-m64 200.i -o native_cpugcc_200.s > native_cpugcc.out 2>&1
echo "native exit: $?"
echo "--- native stdout/stderr ---"
cat native_cpugcc.out
ls -la native_cpugcc_200.s 2>/dev/null || echo "(no output file produced)"

echo
echo "=== PIN+TOOL cpugcc run ==="
rm -f pin_cpugcc_200.s
$PIN -t src/obj-intel64/bprofile.so -prof_time 9999 -- ./cpugcc_r_base.Oz-m64 200.i -o pin_cpugcc_200.s > pin_cpugcc.out 2> pin_cpugcc.err
echo "pin exit: $?"
echo "--- app stdout ---"
cat pin_cpugcc.out
echo "--- tool stderr (grep: stats/verify/error) ---"
grep -E "dead-reg|TCVERIFY|after commit|Invalid|ERROR|failed" pin_cpugcc.err | head
echo "--- tool stderr tail ---"
tail -6 pin_cpugcc.err
ls -la pin_cpugcc_200.s 2>/dev/null || echo "(no output file produced)"

echo
echo "=== partial-output comparison (if both produced any) ==="
if [ -f native_cpugcc_200.s ] && [ -f pin_cpugcc_200.s ]; then
  cmp native_cpugcc_200.s pin_cpugcc_200.s && echo "PARTIAL_OUTPUTS_IDENTICAL" || echo "partial outputs differ"
fi
