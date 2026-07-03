#!/bin/bash
echo "=== pin version ==="
/home/noam/pin/pin -version 2>&1 | head -3
echo "=== git repo state of ~/binary_translation ==="
cd ~/binary_translation && git rev-parse --show-toplevel 2>&1 | head -1
echo "=== git/gh availability in WSL ==="
git --version 2>&1 | head -1
gh auth status 2>&1 | head -4
echo "=== sizes of what a repo would carry ==="
du -sh ~/binary_translation/ex4/src ~/binary_translation/ex4/*.so ~/binary_translation/ex4/ex4.zip 2>/dev/null
