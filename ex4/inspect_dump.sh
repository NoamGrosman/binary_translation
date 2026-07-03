#!/bin/bash
ls /mnt/wslg/dumps/ > /tmp/dumps_list.txt 2>&1
tail -3 /tmp/dumps_list.txt
f="/mnt/wslg/dumps/$(tail -1 /tmp/dumps_list.txt)"
echo "FILE=$f"
file "$f"
gdb -batch -c "$f" ~/binary_translation/ex4/sgcc_base.mytest-m64 \
    -ex 'info registers rax rbx rcx rdx rsi rdi rbp rsp rip r8 r12 r13' \
    -ex 'x/12gx $rsp' 2>/dev/null | head -40
