# Exercise 2 — Edge-Profiling Pintool (JIT mode)

Re-statement of the official PDF (`Exercise2 (2).pdf`), written for our own reference.

## Goal

Build a Pin tool named **`ex2.so`** that runs in **JIT mode** and profiles every
executed basic block and every executed jump (direct + indirect) of a target
binary. The reference target is the supplied `bzip2` running on `input.txt`.

Wall-clock budget on the bzip2 input: **≤ 5 seconds**.

## Part 1 — `edge-profile.csv`

For each basic block (BBL) with a **non-zero** execution count, emit one line.
The lines must be **sorted hottest → coldest** by BBL execution count.

Per-line format (exact, comma-separated):

```
<bbl_addr>, <bbl_exec_count>
                                                  // base fields, always present
, <taken_count>, <fallthru_count>                 // only if BBL terminates with a CONDITIONAL jump
, <tgt_addr_1>, <count_1>, …, <tgt_addr_10>, <count_10>
                                                  // only if BBL terminates with an INDIRECT jump
                                                  // up to 10 targets, sorted hottest → coldest
```

Notes:
- Up to **10,000 BBLs** total may be assumed.
- A BBL may end with neither a conditional nor an indirect jump (fall-through
  end of trace, direct unconditional, return into next BBL, …); those lines
  carry only `<addr>, <count>`.
- Conditional vs. indirect are mutually exclusive at the tail.

## Part 2 — `rtn-output.csv`

> Assumption: the translated program is **single-threaded**.

For **each routine**, dump up to **20 successive values** for each of the
registers `RAX, RBX, RCX, RDX, RSI, RDI`, together with the **average delta**
across those values when one exists.

Sample line shape given by the staff:

```
RAX values: 0x0 -> 0x1000 -> 0x2000 -> 0x1000   Has an Average delta: Yes   Average delta: 0x1000
```

That example has 4 values and 3 deltas: +0x1000, +0x1000, −0x1000. The reported
"Average delta" of `0x1000` matches **mean of absolute deltas**, not signed mean
(signed would be `0x555`). We will follow the absolute-value interpretation.

A line should still be emitted when there are 0 or 1 samples — then
"Has an Average delta: No".

The PDF says "Add the needed fields to the output CSV file for each register
**in each routine**" — i.e., per-routine grouping is required, the register
sequence must travel with its owning routine.

## Tips from the PDF (verbatim spirit)

1. Use `jumpmix.cpp` as a reference for taken vs. fall-through and for
   indirect-jump statistics.
2. Recommended API for indirect targets:
   ```cpp
   INS_InsertCall(tail, IPOINT_BEFORE, AFUNPTR(do_branch_indirect),
                  IARG_BRANCH_TARGET_ADDR, IARG_BRANCH_TAKEN, IARG_END);
   VOID do_branch_indirect(ADDRINT target, BOOL taken) { ... }
   ```

## How to run (per the PDF)

```sh
gunzip bzip2.gz input.txt.gz
<pindir>/pin -t ex2.so -- ./bzip2 -k -f input.txt
```

The bzip2 run compresses `input.txt` → `input.txt.bz2`. Outputs of the tool
land next to the run: `edge-profile.csv`, `rtn-output.csv`.

## Submission

- Submission is **in pairs**.
- Deliverable: **`ex2.zip`** containing
  1. compiled `ex2.so`,
  2. a `src/` directory with all `.cpp`/`.h`, `makefile`, `makefile.rules`,
     and `README.txt` (full names, IDs, build + run instructions).
- Deadline: **Tuesday, June 2 2026, midnight**.
  (Today is 2026-06-06 — the deadline is in the past; current `ex2.zip` is
  dated 2026-06-01, so this is a post-submission review session.)

## Test helper (`tst.c`, supplied separately)

A tiny C program with `foo()`, `bar()`, `gal()`, and a `main` that runs
`gal()` 4 × 1000 = 4000 times. Build with `gcc -o tst tst.c`, then run
`pin -t ex2.so -- ./tst` and check that the call counts come out as
**4000 (or 4000 + 1)** as the staff phrased it. (Source of the "+1" is not
stated — see Open Questions.)

## VM / environment

- PDF text: "checked on the virtual machine (VM) provided in the Moodle
  (**ubuntu18**)".
- Screenshot you attached actually shows **Ubuntu 20.04.6 LTS (Focal Fossa)**.
  This is a discrepancy — open question below.
- Toolchain expected: Pin (JIT mode), gcc.
- README in this folder says it was built/tested with
  `Pin 4.0 (pin-external-4.0-99633-g5ca9893f2-gcc-linux)`.
