#!/bin/bash
mkdir -p ~/binary_translation/ex4/ex2ref
cd ~/binary_translation/ex4/ex2ref
cp -f ../bzip2 .
ln -sf ../input-long.txt input-long.txt
ulimit -c 0
/home/noam/pin/pin -t ~/binary_translation/ex2/obj-intel64/ex2.so -- ./bzip2 -k -f input-long.txt > ex2_run.log 2>&1
echo "EXIT=$?"
ls -la edge-profile.csv
head -5 edge-profile.csv
