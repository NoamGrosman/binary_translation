#!/bin/bash
cd ~/binary_translation
echo "=== remotes ==="
git remote -v
echo "=== branch + last commits ==="
git branch --show-current
git log --oneline -5 2>/dev/null
echo "=== status (porcelain, first 30) ==="
git status --porcelain | head -30
echo "total dirty/untracked: $(git status --porcelain | wc -l)"
echo "=== .gitignore ==="
cat .gitignore 2>/dev/null || echo "(none)"
echo "=== tracked file count + largest tracked ==="
git ls-files | wc -l
git ls-files | xargs -I{} du -k {} 2>/dev/null | sort -rn | head -5
