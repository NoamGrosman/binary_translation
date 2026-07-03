# GOAL — Complete Exercise 4 (optimize the `bprofile.cpp` Pintool) end-to-end

You are working on a university "Binary Translation" course homework. Your goal is to
**modify and optimize the probe-mode Pintool `bprofile.cpp`** so that it satisfies every
requirement below, then **build it, run it, measure it, and prove to yourself that it
works** before you stop.

This is a **goal-driven task, not a checklist to attempt once.** Keep working — edit,
rebuild, rerun, remeasure — in a loop until **every item in the "Definition of Done"
section passes**. Do not declare success until you have observed each success criterion
with your own commands. If something fails, diagnose the root cause and fix it; do not
paper over it or skip it. If you get genuinely stuck on a specific sub-goal after real
effort, say so explicitly and show what you tried — but exhaust the obvious avenues first.

---

## 1. Environment (verify these before you start)

- OS: **WSL Ubuntu** (run all build/run commands inside WSL bash). g++ 13.3 available.
- **Intel Pin** kit root: `PIN_ROOT=/home/noam/pin`, launcher at `/home/noam/pin/pin`.
- Working directory for this exercise: `~/binary_translation/ex4`
  (Windows path `\\wsl.localhost\ubuntu\home\noam\binary_translation\ex4`).
- The exercise 4 folder currently contains **only `bprofile.cpp`** plus the input data.
  There are **no makefiles yet** — you must add them (copy + adapt from
  `~/binary_translation/ex3/src/makefile` and `makefile.rules`, changing the tool name
  to `bprofile`, i.e. `TEST_TOOL_ROOTS := bprofile`).
- Reference material you may read:
  - `~/binary_translation/ex3/src/btranslate.cpp` — same translation framework, working version.
  - `~/binary_translation/ex2/` — the **JIT-mode edge profiler** and its
    `edge-profile.csv` (this is the reference profile for Task 4; see note there).
  - `~/binary_translation/ex2/SPEC.md` — exact definition of the `edge-profile.csv` format.

### Input binaries / run commands (the required test set)

Both of these must build translated code successfully, run to correct completion, and be
profiled + measured:

1. **bzip2** on a 20 MB text input:
   ```
   /home/noam/pin/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./bzip2 -k -f input-long.txt
   ```
   The ex4 `bzip2` binary and `input-long.txt` currently live in nested subfolders
   (`ex4/bzip2/bzip2`, `ex4/input-long.txt/input-long.txt`) and carry Windows
   `:Zone.Identifier` junk files. Normalize this: place `bzip2` and `input-long.txt` in
   the run directory, delete the `Zone.Identifier` files, and `chmod +x bzip2`
   (it is currently non-executable).
2. **cpugcc_r_base.Oz-m64** (the long-running SPEC gcc, from `~/binary_translation/ex3`):
   ```
   /home/noam/pin/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./cpugcc_r_base.Oz-m64 200.i -o 200.s
   ```
   Copy `cpugcc_r_base.Oz-m64` and `200.i` from `~/binary_translation/ex3` as needed.

### Build & tooling notes

- Build with: `make PIN_ROOT=/home/noam/pin obj-intel64/bprofile.so` from the `src` dir
  (verify the exact invocation works; adapt if the makefile expects a different layout).
- Correctness of the target program's output is your ground truth for "registers were
  preserved correctly": compare the bytes of the program's real output
  (bzip2's `input-long.txt.bz2`, cpugcc's `200.s`) between a native run and a
  Pin-instrumented run — they must be identical.
- **`perf` is NOT installed** in this WSL, so the "total cycles" metric is likely
  unavailable. Use **`/usr/bin/time -v`** (GNU time, present at `/usr/bin/time`) which
  reports elapsed (wall), user, system time and major/minor page faults — that covers
  four of the five allowed metrics. Only attempt `perf stat` if you confirm it actually
  works; otherwise ignore the cycles metric.

---

## 2. Background — what `bprofile.cpp` does

`bprofile.cpp` is a **probe-mode** Pintool. At image load it decodes every routine,
re-encodes the instructions into an allocated Translation Cache (TC), and — for each
basic block — inserts a **profiling stub** that increments runtime counters. A helper
thread waits `-prof_time` seconds, then calls `disable_profiling_in_tc()`, which
overwrites the leading NOP of every stub with a 5-byte `jmp` that skips the stub (so
profiling stops and the program runs full-speed for the remainder). On `_exit` it dumps
the profile.

Key data structures / functions to understand before editing:
- `bbl_map_t` (per-BBL: `counter`, `fallthru_counter`, `targ_addr[4]`, `targ_count[4]`).
- `add_profiling_instrs()` — **emits the profiling stub**; this is the hot spot to optimize.
- `disable_profiling_in_tc()` — the runtime "turn profiling off" mechanism; must keep working.
- `find_candidate_rtns_for_tc()` — inserts the stub before each BBL-terminating
  instruction, and a fall-through stub right after each conditional branch.
- `dump_profile()` / `main()` — currently write a human-readable dump to `bprofile.out`.

### Current profiling stub (what to optimize)

For **every** BBL the stub unconditionally does: save RAX to memory → `MOV RAX,[counter]`
→ `LEA RAX,[RAX+1]` → `MOV [counter],RAX` → restore RAX. For BBLs ending in an
**indirect** jump it additionally saves/restores RBX and RCX (each via RAX) and records
the target bucket. That is a lot of memory traffic on the hottest code paths.

---

## 3. Tasks (the five requirements)

### Task 1 — Run correctly on all input binaries
The provided tool must translate and run both binaries above to correct completion. If it
currently fails on either (crash, wrong output, or a broken `disable_profiling_in_tc`
skip-jump), find and fix the cause. Success = program exit code 0 **and** byte-identical
program output vs. a native run, on both binaries.

### Task 2 — Skip save/restore of DEAD registers (the core optimization)
The profiling stub preserves RAX (and RBX/RCX, and implicitly relies on RFLAGS) so it
doesn't corrupt the application. But at many instrumentation points some of these are
**dead** (their current value is never read again before being overwritten). For those,
**omit the save and restore.** Concretely:

- **RAX dead** ⇒ drop the `MOV [rax_mem],RAX` save and the `MOV RAX,[rax_mem]` restore.
- **RBX / RCX dead** (indirect stub) ⇒ drop their two-step save/restore.
- **RFLAGS dead** at the point *and* the terminating instruction is not a conditional
  branch ⇒ you can replace the whole 5-6-instruction RAX-based increment with a single
  `inc qword ptr [counter]` (or `add qword ptr [counter], 1`), eliminating RAX use
  entirely. This is the biggest win. **Do not** use a flag-modifying memory increment
  when the terminator is a `Jcc` (it reads RFLAGS) or when flags are otherwise live.

You decide how to compute liveness (e.g. a backward scan over each routine's decoded
instructions using Pin's `INS_RegR`/`INS_RegW` or XED's read/written register sets;
`ret`/indirect-jmp-through-RAX keep RAX live; a `ret` keeps RAX live as the return value).
Be **conservative**: when in doubt, treat a register/flag as live and keep the
save/restore — correctness first, speed second.

**Preserve the disable mechanism:** each stub must still begin with a skippable
wide-NOP head that is ≥5 bytes so `disable_profiling_in_tc()` can overwrite it with the
5-byte skip `jmp`. If you change the stub, keep that invariant or update the disable
logic to match.

**Correctness check for this task:** with the optimization on, both target programs still
produce byte-identical output, and the emitted profile counts are unchanged vs. the
unoptimized tool (the counts don't depend on register preservation).

### Task 3 — Emit `edge-profile.csv` in the exercise-2/3 format
Replace the `bprofile.out` output with a file named **`edge-profile.csv`** (change the
`ofstream` in `main()` and rewrite the dump routine). Format, one line per BBL with a
**non-zero** execution count:

```
<bbl_addr>, <exec_count>[, <taken_count>, <fallthru_count>][, <t_addr1>, <cnt1>, ... up to 4]
```
- `exec_count` = `bbl_map[b].counter`.
- Emit `taken, fallthru` **only when the BBL terminates with a conditional branch**, where
  `fallthru = bbl_map[b].fallthru_counter` and `taken = counter - fallthru_counter`.
  (Sanity: `taken + fallthru == exec_count`, matching `ex2/edge-profile.csv`.)
- Emit indirect-target pairs **only when the BBL terminates with an indirect jump**, up to
  4 targets (`MAX_TARG_ADDRS+1`), skipping empty slots, **sorted by count descending**.
- **Sort all emitted lines hottest → coldest** by `exec_count`.
- Addresses in `0x...` hex, counts in decimal, comma-space separated, matching
  `ex2/edge-profile.csv`. Assume ≤ 10,000 BBLs.

### Task 4 — Explain large profile differences in the README
The reference is the **exercise-2 JIT-mode edge profiler** (`~/binary_translation/ex2/ex2.so`,
which emits `edge-profile.csv`). The PDF calls it "exercise 3," but ex3 is a translator that
emits no profile — ex2 is the only prior edge profiler, and no separate reference file was
provided by the course, so ex2 is the intended baseline. The committed
`ex2/edge-profile.csv` was generated on the short `input.txt`, so for an apples-to-apples
comparison **build and run `ex2.so` (JIT mode) on the same binary + input you profile in
ex4** (bzip2 on `input-long.txt`) to produce a matched reference. Diff that against this
tool's `edge-profile.csv`, identify the **large** discrepancies, and explain their cause in
`README.txt` (this is an *explanation* task — you are not fixing the differences, just
accounting for them). Expected sources of difference to investigate and describe:
probe-mode (routine-granularity translation, only routines that pass
`RTN_ReplaceProbed`) vs. JIT-mode full coverage; the `-prof_time` sampling window vs. a
full-run count; the indirect-target bucketing (`AND RAX, 3` hashes targets into 4 slots by
low address bits, so target addresses/counts are approximate); and any routines not
translated in probe mode.

### Task 5 — Prove ≥5% improvement on at least one metric
Build **two** tools: the unmodified original (`bprofile_orig.so`) and your optimized one
(`bprofile.so`). For at least one target binary, using the same `-prof_time <N>`, show the
optimized tool improves **≥5%** on at least one of: elapsed time, user time, system time,
or page faults. Method:
- Pick a `-prof_time N` large enough that a meaningful fraction of the run is spent with
  profiling **on** (so the optimization can show up); cpugcc is the more reliable target.
- Measure with `/usr/bin/time -v`, run each configuration **≥5 times**, and compare
  **median (or min)** to reduce noise.
- Report a before/after table in `README.txt`, with the metric(s) that cleared 5%.

---

## 4. Deliverables (produce all of these under `~/binary_translation/ex4`)

- `src/bprofile.cpp` — the modified source.
- `src/makefile`, `src/makefile.rules` — building `bprofile.so` (adapted from ex3).
- `src/README.txt` (a.k.a. the submission README) containing:
  - (a) names + IDs: **Noam Grosman — 318677341** and **Or Ederi — 314814849**.
  - (b) how to run the tool (exact pin command lines for both binaries).
  - (c) what problems you fixed in the pintool and how you chose to fix them
    (Task 1 fixes + Task 2 dead-register strategy).
  - (d) the large profiling differences vs. the exercise-2 profile and why they happen.
  - (e) the ≥5% performance improvement table (Task 5).
- The built `bprofile.so`.
- `ex4.zip` = the built `bprofile.so` + the `src/` directory (matching the submission
  requirement: one zip named `ex4.zip` with `bprofile.so` and `src/`).

---

## 5. Definition of Done — loop until ALL of these are observed true

Run the actual commands and confirm each before finishing:

1. `bprofile.so` **compiles cleanly** with the ex4 makefiles (no errors).
2. **bzip2** run: exit code 0, and `input-long.txt.bz2` is **byte-identical** to a native
   `./bzip2 -k -f input-long.txt` run.
3. **cpugcc** run: exit code 0, and `200.s` is **byte-identical** to a native run's `200.s`.
4. An `edge-profile.csv` is produced for a run, is **sorted hottest→coldest**, uses the
   exact format in Task 3, and (spot-check) `taken + fallthru == exec_count` on
   conditional-branch lines.
5. The optimized tool's profile counts **match** the unoptimized tool's counts on the same
   run (optimization changed cost, not results).
6. The dead-register optimization is actually in effect (you can point to stubs where
   save/restore was omitted / replaced by a direct memory increment).
7. `/usr/bin/time -v` medians over ≥5 runs show **≥5% improvement** on at least one allowed
   metric for at least one binary; the table is in `README.txt`.
8. `README.txt` covers items (a)–(e); `ex4.zip` exists with the right contents.

Only when all eight hold: summarize what you changed, the measured improvement, and the
key profile differences you found. Until then, keep iterating.

## 6. Working method
- Make the smallest change that moves a success criterion from failing to passing, then
  rebuild and reverify — don't batch many risky changes blind.
- Preserve the existing translation framework; you're modifying the stub emission,
  the output/dump, and (minimally) fixing any run failures — not rewriting the tool.
- Keep an unmodified copy of the original tool around to build `bprofile_orig.so` for the
  A/B measurement.
- Show your measurement commands and their raw output in your final summary so the numbers
  are auditable.
