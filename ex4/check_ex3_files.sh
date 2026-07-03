#!/bin/bash
cd ~/binary_translation/ex3
echo "=== sizes ==="
ls -la cpugcc_r_base.Oz-m64 cpugcc_r_base.Oz-m64_ sgcc_base.mytest-m64 sgcc_peak.mytest-m64 2>/dev/null
echo
echo "=== ELF type (EXEC = non-PIE, DYN = PIE) ==="
for f in cpugcc_r_base.Oz-m64 cpugcc_r_base.Oz-m64_ sgcc_base.mytest-m64 sgcc_peak.mytest-m64; do
  [ -f "$f" ] && echo -n "$f: " && readelf -h "$f" | grep -E 'Type:' | tr -s ' '
done
echo
echo "=== do the two cpugcc files differ? ==="
cmp cpugcc_r_base.Oz-m64 cpugcc_r_base.Oz-m64_ && echo IDENTICAL || echo DIFFER
echo
echo "=== does the _ variant still contain the kmovd at 0x203cfb? ==="
for f in cpugcc_r_base.Oz-m64 cpugcc_r_base.Oz-m64_; do
  echo -n "$f @0x203cf8-203d00: "
  xxd -s 0x203cf0 -l 32 "$f" | head -2
done
echo
echo "=== any AVX-512 EVEX-prefixed instrs in each (objdump grep, count) ==="
which objdump >/dev/null 2>&1 && for f in cpugcc_r_base.Oz-m64_ sgcc_base.mytest-m64; do
  echo -n "$f kmov/EVEX count: "
  objdump -d "$f" 2>/dev/null | grep -cE 'kmov|%zmm|\{k[0-9]\}'
done
echo
echo "=== notes/docs mentioning the _ file or avx ==="
grep -l -i "Oz-m64_\|avx" Exercise3.md WORK_SUMMARY.md *.log 2>/dev/null | head
grep -i -h "Oz-m64_\|avx" Exercise3.md WORK_SUMMARY.md 2>/dev/null | head -10
