#!/bin/bash
cd ~/binary_translation/ex4
for i in 1 2 3; do
  /usr/bin/time -f "native bzip2 run$i wall=%e user=%U" ./bzip2 -k -f input-long.txt 2>&1 | tail -1
done
uptime
