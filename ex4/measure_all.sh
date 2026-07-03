#!/bin/bash
cd ~/binary_translation/ex4
bash measure.sh 5 9999 bzip
cp measure_results.txt measure_bzip.txt
bash measure.sh 5 10 sgcc
cp measure_results.txt measure_sgcc.txt
echo "=== BZIP2 (prof_time 9999) ==="
cat measure_bzip.txt
echo "=== SGCC (prof_time 10) ==="
cat measure_sgcc.txt
