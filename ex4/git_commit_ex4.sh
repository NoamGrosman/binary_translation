#!/bin/bash
# Stage + commit ex4 (respecting the new .gitignore), then push.
cd ~/binary_translation || exit 1

# the friend's A/B baseline tool, tracked at ex4 root like bprofile.so
cp ex4/src/obj-intel64/bprofile_orig.so ex4/bprofile_orig.so 2>/dev/null

git add .gitignore ex4
echo "=== what will be committed (ex4 summary) ==="
git status --porcelain | head -60
echo "total staged: $(git status --porcelain | grep -c '^A')"
echo "=== staged size check (top 8) ==="
git diff --cached --stat | tail -3
git ls-files --cached ex4 | xargs -I{} du -k {} 2>/dev/null | sort -rn | head -8

git commit -m "ex4: bprofile with dead-register optimization + all correctness fixes

- Task 1-5 complete; five extra fixes merged after cross-review vs partner's
  version (PIE jump tables, RTN_Close on error paths, flag-free SHLX/SHRX
  indirect masking, routine-end fall-through jmp, probe-safety guards)
- verified: bzip2/sgcc_base/sgcc_peak/cc1 byte-identical outputs (repeated
  runs), TCVERIFY clean, opt-vs-orig profile identity on bzip2
- cpugcc: full PIE translation + native-equivalent SIGILL on non-AVX512 CPU;
  run_zen4_test.sh + README_TEST.md added for full verification on an
  AVX-512 machine
- submission artifact: ex4/ex4.zip

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
echo "commit rc=$?"
git log --oneline -1
echo "=== pushing ==="
git push origin main
echo "push rc=$?"
