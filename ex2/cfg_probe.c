/* CFG probe: hits every branch flavor the pintool should classify. */
#include <stdio.h>

#define COND_ITERS 100
#define INDJMP_ITERS 50
#define INDCALL_ITERS 30
#define RAX_WRITES 25
#define HOT_LOOP_ITERS 1000

static volatile int sink;

/* A routine that writes RAX 25+ times via 25 distinct return values.
   We force successive writes to RAX inside the routine. */
__attribute__((noinline)) long rax_heavy(long seed)
{
    long a = seed;
    /* 25 explicit RAX-touching ops; each `a = ...` should map to a write
       to %rax after compilation with -O0. */
    a += 1;  a += 2;  a += 3;  a += 4;  a += 5;
    a += 6;  a += 7;  a += 8;  a += 9;  a += 10;
    a += 11; a += 12; a += 13; a += 14; a += 15;
    a += 16; a += 17; a += 18; a += 19; a += 20;
    a += 21; a += 22; a += 23; a += 24; a += 25;
    return a;
}

__attribute__((noinline)) void called_once(void)
{
    sink = 42;
}

__attribute__((noinline)) int target_a(int x) { return x + 1; }
__attribute__((noinline)) int target_b(int x) { return x + 2; }
__attribute__((noinline)) int target_c(int x) { return x + 3; }

typedef int (*fp_t)(int);

int main(void)
{
    /* Conditional branch with mixed taken/not-taken: 100 iterations,
       branch on parity → ~50 taken, ~50 not-taken. */
    int t = 0, nt = 0;
    for (int i = 0; i < COND_ITERS; i++) {
        if (i & 1) { t++; } else { nt++; }
    }
    sink = t + nt;

    /* Indirect call (function-pointer dispatch) hitting 3 targets unevenly. */
    fp_t table[3] = { target_a, target_b, target_c };
    long acc = 0;
    for (int i = 0; i < INDCALL_ITERS; i++) {
        /* target_a hit ~half the time, target_b ~third, target_c rest. */
        int k = (i % 6 < 3) ? 0 : (i % 6 < 5) ? 1 : 2;
        acc += table[k](i);
    }
    sink = (int)acc;

    /* Indirect jump (computed goto via switch jump-table). gcc -O0 may
       emit a jump table for a switch on a small range. */
    int s = 0;
    for (int i = 0; i < INDJMP_ITERS; i++) {
        switch (i % 5) {
            case 0: s += 10; break;
            case 1: s += 20; break;
            case 2: s += 30; break;
            case 3: s += 40; break;
            case 4: s += 50; break;
        }
    }
    sink = s;

    /* RAX-heavy routine, called once (forces 25 writes inside one call). */
    sink = (int)rax_heavy(7);

    /* Routine called exactly once. */
    called_once();

    /* Hot loop to make a clearly-hottest BBL. */
    long h = 0;
    for (int i = 0; i < HOT_LOOP_ITERS; i++) {
        h += i;
    }
    sink = (int)h;

    return 0;
}
