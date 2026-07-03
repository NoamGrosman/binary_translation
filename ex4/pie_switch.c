/*
 * PIE test for the ex4 pintool: a routine that ends with an indirect jump
 * through a RIP-based memory operand ("jmp *slot(%rip)"). In a PIE the
 * absolute address of the slot does not fit in a 32-bit displacement, so
 * translating this exercises the scratch-register conversion in
 * add_profiling_instrs() (and the RTN_Close error paths if it ever fails).
 *
 * Build:  gcc -O2 -o pie_switch pie_switch.c   (PIE by default on Ubuntu)
 * The program prints a deterministic checksum; a run under the tool must
 * print the same value as a native run.
 */
#include <stdio.h>

long run_dispatch(long x);

void *jump_slot; /* holds &tgt0 or &tgt1; flipped by main */

__asm__(
    ".text\n"
    ".globl run_dispatch\n"
    ".type run_dispatch,@function\n"
    "run_dispatch:\n"
    "    jmp *jump_slot(%rip)\n"
    ".size run_dispatch, .-run_dispatch\n"
    ".globl tgt0\n"
    ".type tgt0,@function\n"
    "tgt0:\n"
    "    lea 1(%rdi), %rax\n"
    "    ret\n"
    ".size tgt0, .-tgt0\n"
    ".globl tgt1\n"
    ".type tgt1,@function\n"
    "tgt1:\n"
    "    lea 3(%rdi), %rax\n"
    "    ret\n"
    ".size tgt1, .-tgt1\n");

extern char tgt0_sym __asm__("tgt0");
extern char tgt1_sym __asm__("tgt1");

int main(void)
{
    long s = 0;
    for (long k = 0; k < 20000000; k++) {
        jump_slot = (k & 1) ? (void *)&tgt1_sym : (void *)&tgt0_sym;
        s = (s + run_dispatch(s)) & 0xffffff;
    }
    printf("%ld\n", s);
    return 0;
}
