#!/bin/bash
# Setup + characterize the newly-provided cc1: extract, ELF type, AVX-512 risk,
# native reference run.
cd ~/binary_translation/ex4 || exit 1
ulimit -c 0

echo "=== extract ==="
cp /mnt/c/Users/noam0/Downloads/cc1.gz .
gunzip -f cc1.gz
chmod +x cc1
ls -la cc1

echo
echo "=== ELF type / linking ==="
readelf -h cc1 | grep -E 'Type:' | tr -s ' '
ldd cc1 2>&1 | head -5

echo
echo "=== AVX-512 content (EVEX/zmm/kmov sites in disasm) ==="
objdump -d cc1 2>/dev/null | grep -cE 'kmov|%zmm|\{k[0-9]\}'

echo
echo "=== native run (reference) ==="
rm -f native_cc1_200.s
/usr/bin/time -f "native: wall=%e user=%U" ./cc1 200.i -o native_cc1_200.s > native_cc1.out 2>&1
echo "native exit: $?"
tail -3 native_cc1.out
ls -la native_cc1_200.s 2>/dev/null || echo "(no output)"
