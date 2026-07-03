#!/bin/bash
cd ~/binary_translation/ex4
ulimit -c 0
R=cpugcc_equiv_result.txt
: > $R

./cpugcc_r_base.Oz-m64 200.i -o native_cpugcc_200.s > native_cpugcc.out 2>&1
echo "NATIVE_EXIT=$?" >> $R
[ -f native_cpugcc_200.s ] && echo "NATIVE_OUT=yes($(stat -c%s native_cpugcc_200.s))" >> $R || echo "NATIVE_OUT=none" >> $R

/home/noam/pin/pin -t src/obj-intel64/bprofile.so -prof_time 9999 -- ./cpugcc_r_base.Oz-m64 200.i -o pin_cpugcc_200.s > pin_cpugcc.out 2> pin_cpugcc.err
echo "PIN_EXIT=$?" >> $R
[ -f pin_cpugcc_200.s ] && echo "PIN_OUT=yes($(stat -c%s pin_cpugcc_200.s))" >> $R || echo "PIN_OUT=none" >> $R
echo "PIN_APP_STDOUT: $(wc -c < pin_cpugcc.out) bytes" >> $R

cat $R
