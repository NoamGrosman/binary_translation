#!/bin/bash
cd ~/binary_translation/ex4
EX4=edge-profile.opt.csv        # bprofile.so full-run (prof_time 9999) on bzip2
EX2=ex2ref/edge-profile.csv     # ex2.so JIT full-run on same bzip2 + input

echo "== line counts =="
wc -l $EX4 $EX2

echo "== identical lines =="
comm -12 <(sort $EX4) <(sort $EX2) | wc -l

echo "== top 10 ex4 =="
head -10 $EX4
echo "== top 10 ex2 =="
head -10 $EX2

echo "== BBLs only in ex4 (top 10 by count) =="
join -v1 -t, <(sort -t, -k1,1 $EX4) <(sort -t, -k1,1 $EX2) | sort -t, -k2,2 -rn | head -10
echo "== BBLs only in ex2 (top 10 by count) =="
join -v2 -t, <(sort -t, -k1,1 $EX4) <(sort -t, -k1,1 $EX2) | sort -t, -k2,2 -rn | head -10

echo "== same BBL addr, different counts (top 10 by ex2 count) =="
join -t, <(sort -t, -k1,1 $EX4) <(sort -t, -k1,1 $EX2) | awk -F, '$2+0 != $5+0' | head -10

echo "== indirect-jump lines in each =="
grep -cE ', 0x' $EX4
grep -cE '(, 0x[0-9a-f]+){2,}' $EX2
echo "== sample indirect lines ex4 =="
grep -E ',.*, 0x' $EX4 | head -5
echo "== sample indirect lines ex2 =="
awk -F', ' 'NF>=4 && $3 ~ /^0x/' $EX2 | head -5
