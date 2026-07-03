# Exercise 4 — Work Summary (session handoff)

**Status: COMPLETE (revised 2026-07-03).** All 8 Definition-of-Done items from `FABLE_PROMPT.md` verified on 2026-07-02; on 2026-07-03 five additional fixes were merged after comparing against a friend's submission (see §8 at the end — READ IT, it supersedes parts of §4/§5).
This document is the full record of what was done, why, how it was verified, and where everything lives.

---

## 1. The task

Optimize the probe-mode Pintool `bprofile.cpp` (course-provided binary translator that
inserts BBL-counting stubs into a Translation Cache), per `FABLE_PROMPT.md`:

1. **Task 1** — make it run both test binaries to correct completion.
2. **Task 2** — skip save/restore of DEAD registers in the profiling stubs (core optimization).
3. **Task 3** — emit `edge-profile.csv` in the exercise-2 format instead of `bprofile.out`.
4. **Task 4** — explain large profile differences vs the ex2 JIT profiler in the README.
5. **Task 5** — prove ≥5% improvement on ≥1 metric with an A/B measurement (≥5 runs, medians).

## 2. Environment facts (critical for reproducing)

- WSL Ubuntu, `PIN_ROOT=/home/noam/pin`, work dir `~/binary_translation/ex4`
  (Windows UNC: `\\wsl.localhost\ubuntu\home\noam\binary_translation\ex4`).
- Build: `cd src && make PIN_ROOT=/home/noam/pin obj-intel64/bprofile.so obj-intel64/bprofile_orig.so`
  (makefiles adapted from ex3, `TEST_TOOL_ROOTS := bprofile bprofile_orig`).
- **`cpugcc_r_base.Oz-m64` CANNOT run on this machine at all.** It contains an unconditional
  AVX-512 instruction (`kmovd k1,eax` at offset 0x203cfb, inside `ggc_min_heapsize_heuristic`)
  and the CPU (Intel Core Ultra 7 155H, Meteor Lake) has **no AVX-512**. It dies with SIGILL
  even natively without Pin (verified; ex3's logs show the same core dump). The long-running
  gcc target used instead is **`sgcc_base.mytest-m64`** (same SPEC gcc compiled without
  AVX-512, copied from ex3) — statically linked ~30 MB, so its glibc is translated too
  (a *harder* test). Documented in README section (b).
- `perf` is not installed → metrics via `/usr/bin/time -v`. No passwordless sudo.
- Run commands:
  - `pin -t obj-intel64/bprofile.so -prof_time <N> -- ./bzip2 -k -f input-long.txt`
  - `pin -t obj-intel64/bprofile.so -prof_time <N> -- ./sgcc_base.mytest-m64 200.i -o 200.s`
- Reference outputs for byte-compare: `native_input-long.txt.bz2`, `native_200.s` (from native runs).
- **Tooling gotchas:** invoke WSL via script files (`wsl.exe -d ubuntu bash /path/script.sh`);
  inline quoting through Git-Bash/PowerShell mangles `$?`/heredocs. Git Bash path-converts
  `/home/...` args — use PowerShell for `wsl.exe` calls. WSL crash dumps are captured by
  `wsl-capture-crash` and are not readily accessible; the tool has its own `-catch_segv`
  register-dump handler instead (probe mode shares the app's process).

## 3. Deliverables (all under `~/binary_translation/ex4/`)

| File | What it is |
|---|---|
| `src/bprofile.cpp` | Final optimized tool (Tasks 1+2+3 + all bug fixes) |
| `src/bprofile_orig.cpp` | Baseline for the A/B: same Task-1 fixes + Task-3 CSV, but **original unoptimized stub emission** |
| `src/makefile`, `src/makefile.rules` | Build files (from ex3, tool roots renamed) |
| `src/README.txt` | Submission README, items (a)–(e) complete |
| `bprofile.so` | Built optimized tool (copy of `src/obj-intel64/bprofile.so`) |
| `ex4.zip` | Submission: `bprofile.so` + the 5 `src/` files |
| `measure_bzip.txt`, `measure_sgcc.txt` | Raw Task-5 measurement rows |
| `edge-profile.csv` | Sample output from a final bzip2 run |
| `ex2ref/edge-profile.csv` | Task-4 reference: ex2.so (JIT) run on the same bzip2 + input-long.txt |
| `*.sh`, `*.log`, `dbg/` | Working scripts/logs from the debugging campaign (not in the zip) |

## 4. What was changed in the tool

### Task 1 correctness fixes (in BOTH bprofile.cpp and bprofile_orig.cpp)

1. **Jcc → untranslated target** aborted translation (`fix_direct_jmp_or_call_to_orig_addr`
   handles only CALL/JMP). Fix in `fix_direct_jmp_or_call_displacement`: Jcc has no
   memory-indirect form, so its rel32 is pointed back at the original target.
   Without this, sgcc silently ran UNtranslated (no profiling at all).
2. **Skip `.plt`/`.plt.got`/`.plt.sec`/`.init`/`.fini` sections** — probe-replacing the ELF
   startup stubs corrupted the static binary (SIGSEGV at startup). Mirrors ex3's fix.
3. **Skip routines containing LOOP/LOOPcc/JRCXZ** (ex3's "Bug B"): these have only a 1-byte
   displacement; stubs inflate distances past ±127 and the re-encode silently WRAPS the
   displacement (branch lands 256/512 bytes off, mid-instruction). sgcc has exactly two
   (`jrcxz` in `__mpn_add_n`/`__mpn_sub_n`). Found byte-level by the TCVERIFY pass (below).
4. **mmap moved ~1 GB above the image** (`allocate_and_init_memory`,
   `aligned_target = highest_addr + 0x40000000`). THE nastiest bug, see §6.
5. **Race-free profiling disable**: `disable_profiling_in_tc` used to memcpy 5-byte jmps
   over stub heads while the app executes them (torn write → wild jump).
   `PIN_StopApplicationThreads` is broken in probe mode (SIGTRAP kills). Final design:
   the timer thread only *prepares* the patch list; `kill(getpid(), SIGUSR2)` (SIGUSR2
   blocked on the timer thread) delivers to the app thread, and the patches are applied
   inside the handler — the single app thread cannot be mid-stub while in its own handler.
   5s-timeout fallback patches directly.

### Task 2 optimization (bprofile.cpp only)

- **Liveness**: per-routine backward dataflow fixpoint over RAX/RBX/RCX + status flags
  (one unit). GPR reads/writes from Pin (`INS_RegR/W` + `REG_FullRegName`); only 64/32-bit
  writes kill (32-bit zero-extends); predicated instructions (CMOVcc/REP,
  `INS_IsPredicated`) never kill. Flags from XED rflags info: read if any status flag read,
  killed only if all six unconditionally written/undefined (`must_write`).
  Boundary conditions (conservative): `ret` → RAX+RBX live, RCX+flags dead;
  `call` → reads RAX+RCX, kills RAX/RCX/flags, RBX flows through; indirect jmp →
  RAX/RBX/RCX live, flags dead (the stub's own `AND` always clobbered them — original
  tool's assumption); any unseen target → everything live. Syscalls/interrupts → read all.
- **Stub emission** (`add_profiling_instrs`, now takes a `dead_mask`):
  - flags dead (or indirect stub) → whole increment collapses to
    `NOP ; ADD qword ptr [rip→counter], 1` — no register use at all. Requires the counter
    within disp32: **`bbl_map` was moved from the heap into the TC's RWX mmap region**
    (after TC + jump map; new `max_bbl_count` bound captured before the ×10 inflation).
    The framework's existing `orig_rip_addr`/`fix_rip_displacement` machinery does the
    final displacement fixup; emission verifies the encoding landed exactly on the counter.
  - RAX dead → drop `MOV [rax_mem],RAX` save and `MOV RAX,[rax_mem]` restore.
  - RBX/RCX dead → drop their save/restore pairs in the indirect stub (never fires in
    practice due to the conservative indirect boundary — fine).
  - The ≥5-byte wide-NOP stub head is unchanged, so the disable mechanism is intact.
  - Stub-site masks: terminator stub uses live-in of the terminating instruction;
    fall-through stub (after Jcc) uses live-in of the next instruction.
- Debug knobs (default off, kept): `-no_mem_add`, `-no_skip_dead`, `-skip_lo/-skip_hi`
  (per-stub-ordinal bisection), `-report_stub N`, `-dump_around 0xADDR`, `-poison_rax`,
  `-dummy_clobber`, `-heap_bbl`, `-catch_segv` (SIGSEGV register dump).
- **TCVERIFY** (always on, prints once after translation): decodes the final TC and checks
  every branch with a known target entry AND every RIP-relative memory operand lands
  exactly where intended. Good translation prints `TCVERIFY: 0 bad branch/rip targets`.

### Task 3 output (both tools)

`dump_profile()` rewritten; `main()` opens `edge-profile.csv`. Per BBL with counter>0:
`0x<bbl_addr>, <exec>` + (`, taken, fallthru` iff terminator is COND_BR, taken = counter −
fallthru) + (indirect target pairs sorted by count desc, only non-empty buckets).
Lines sorted hottest→coldest. BBL addr = orig addr of first non-ProfilingIns entry from
`starting_ins_entry` (skips the previous BBL's fall-through stub sharing the entry range).

## 5. Verification results (all observed, reproducible)

- Clean rebuild from scratch: OK (`-Werror`, no warnings/errors).
- **bzip2**: exit 0, output byte-identical (`cmp`) to native — many runs, both tools.
- **sgcc**: exit 0, `200.s` byte-identical — **6/6 consecutive runs per tool** after all fixes
  (matrix_test2.sh; earlier flakiness fully explained by §6).
- **Counts unchanged by the optimization**: with `-prof_time 9999` (never disabled,
  deterministic), optimized vs unoptimized `edge-profile.csv` on bzip2 are **byte-identical**
  (`test_counts.sh` → `PROFILES_IDENTICAL`, 872 lines each).
- CSV format checks: `sort -rn -c` passes (sorted desc); `taken+fallthru == exec` holds on
  every conditional line (awk check).
- Optimization is in effect: bzip2 — 2834/4136 stubs use the single ADD, 1013 more skip the
  RAX save; sgcc — 458,051/753,208 ADD, 30,221 RAX-skips (printed by the tool; stub dumps
  via `-dump_around` show the actual shortened sequences).
- **Task 5 table** (medians of 5 runs, `/usr/bin/time -v`, same `-prof_time` for both tools):

  | binary | metric | orig | opt | improvement |
  |---|---|---|---|---|
  | bzip2, prof_time 9999 | wall | 7.71 s | 6.17 s | **20.0%** |
  | bzip2, prof_time 9999 | user | 7.49 s | 6.01 s | **19.8%** |
  | sgcc, prof_time 10 | wall | 54.92 s | 51.68 s | **5.9%** |
  | sgcc, prof_time 10 | minor faults | 927,857 | 893,008 | 3.8% |

  ≥5% met on BOTH binaries (elapsed time). Raw rows in `measure_*.txt` and README (e).

- **Task 4** (README (d)): reference regenerated by running `~/binary_translation/ex2/obj-intel64/ex2.so`
  (JIT) on the same bzip2+input (in `ex2ref/`). 447 of our 872 lines byte-identical to ex2's
  4787 (entire hot loop matches verbatim, e.g. `0x40a7c3, 148638693, 137754781, 10883912`).
  Explained differences: (1) probe-mode coverage — no shared libs (ex2 has a 12.6M-count
  libc BBL), unprobeable/skipped routines; (2) BBL segmentation — JIT ends BBLs at calls and
  discovers heads dynamically at indirect/return targets (ex2-only heads 0x40ad2e/0x40ae1c/
  0x40af0a inside mainSimpleSort; our extra head 0x40a78b is a fall-through continuation);
  (3) indirect targets — we record only true indirect JUMPS into 4 buckets hashed by
  `addr & 3` (approximate, ≤4), ex2 records returns too with up to 10 exact targets
  (mainGtU's `pop rbp; ret` epilogue 0x40abe6: same 50,841,127 count in both, targets only
  in ex2); (4) `-prof_time` prefix-window sampling vs full-run counts.

## 6. The debugging campaign (read this before touching the tool again)

The optimized tool initially crashed on sgcc only. The investigation found **three
independent defects**, and the first two produced wildly misleading signals:

1. **jrcxz displacement wrap** (deterministic *given a layout*, flips with any layout
   change). Found by: per-stub-ordinal bisection (`-skip_lo/hi`, ~20 runs) pointing at an
   innocent stub → same-size/same-semantics discriminator (`-dummy_clobber`) →
   byte-level TC verifier which caught 2 branches landing exactly +0x200 past their
   targets (1-byte disp truncation of −430 → +82). The "culprit stub" from bisection was
   a red herring — it merely changed layout/probabilities.
2. **mmap-on-brk squatting** (the `0x830` crashes). Signature: `memcpy` faulting with
   `rdi=0x830`, sometimes at its *original* address. `-catch_segv` register dump + mapping
   the stack return address showed the caller is `__libc_setup_tls`: `rcx=0` because
   `(sbrk_ret + align−1) & −align == 0` ⟺ **`__sbrk` returned −1** — the tool's region sat
   in the brk growth path, and glibc's pre-malloc TLS sbrk has no failure check.
   Intermittent because the kernel randomizes the brk base per run. Dynamic bzip2 immune
   (its heap isn't after the main image). **Lesson: on this setup, intermittent ≠ race —
   ASLR turns deterministic bugs probabilistic; single-run PASS/FAIL discrimination is
   worthless. Always ≥6 repeats.**
3. **Torn-write disable race** (post-"disabling" crashes, incl. one SIGTRAP from the failed
   `PIN_StopApplicationThreads` attempt) → SIGUSR2-handler patching (§4.5).

The Task-2 liveness analysis itself was **never wrong** — every suspected unsoundness
traced back to one of the three framework defects above.

## 7. If someone needs to re-verify quickly

```bash
cd ~/binary_translation/ex4/src
make PIN_ROOT=/home/noam/pin obj-intel64/bprofile.so obj-intel64/bprofile_orig.so
cd ..
bash final_dod.sh        # clean rebuild + both binaries + CSV checks + zip listing
bash matrix_test2.sh     # 6x sgcc per tool + bzip2 sanity (stability)
bash test_counts.sh      # optimized-vs-orig count identity on full-run bzip2
bash measure_all.sh      # Task-5 A/B timings (10 bzip2 + 10 sgcc runs, ~10 min)
```

Expected markers: `BUILD_OK`, `BZ2_BYTE_IDENTICAL`, `SGCC_BYTE_IDENTICAL`,
`SORTED_DESC_OK`, `TAKEN_PLUS_FT_OK`, `PROFILES_IDENTICAL`,
`TCVERIFY: 0 bad branch/rip targets`, `pass=6 fail=0` per tool.

## 8. 2026-07-03 revision (merge of the better parts of a friend's submission)

After a side-by-side comparison with a friend's ex4.zip, five fixes were added to
BOTH tools (all verified; `v1_build.sh`/`v2_smoke.sh`/`v3_matrix.sh`/`v9_*.sh` are the
scripts; `measure_interleaved.sh` replaced `measure.sh` for Task 5):

1. **PIE support** (`add_profiling_instrs`): when the absolute address of a
   `jmp [rip+disp]` operand doesn't fit disp32 (PIE at 0x55...), load it into a
   scratch reg (RBX, or RCX if RBX is the index) with a 64-bit imm MOV. Verified
   with `pie_switch.c` (synthetic PIE with `jmp *slot(%rip)`): output identical,
   both indirect targets profiled 10M/10M, TCVERIFY 0.
2. **RTN_Close on every error path** of `find_candidate_rtns_for_tc` (was: Pin
   "Must use RTN_Close" assert → exit 127 on any translation abort).
3. **THE BIG ONE — flags live across indirect jumps**: sgcc_peak (-O3) keeps a
   comparison result in RFLAGS across an indirect jump; the stub's `AND RAX,3`
   (and the forced mem-add) corrupted it → **silently wrong output, reproduced
   here** (200.s 3,873,859 vs native 3,873,835 bytes, exit 0!). Fixed with
   flag-free `MOV RCX,62; SHLX; SHRX`; liveness boundary at indirect jmp changed
   to flags-LIVE; `use_mem_add` now requires genuinely dead flags (no more
   `|| is_indirect`). §4's old "indirect jmp ⇒ flags dead" note is WRONG — do not
   reintroduce it. After: sgcc_peak 6/6 byte-identical.
4. **Routine-end fall-through**: routines ending in a fall-through-capable
   instruction now get an appended `jmp <fallthru addr>` + closing BBL stub
   (otherwise TC execution would run into the next routine's translation).
5. **Probe-safety guards** at commit: skip `!RTN_IsSafeForProbedReplacement` and
   `RTN_Size < 8`.

Also: **CSV format now emits `taken, fallthru` on EVERY line** (ex4 PDF format;
uncond/ret ⇒ taken=exec, call/target-split ⇒ fallthru=exec) — this changed the
ex2-comparison counts in README (d) to 685 addr+count matches / 254 byte-identical
of 870 lines. **README ships under BOTH names** — `src/README.md` (proper) and
`src/REDAME.txt` (the PDF's literal, typo'd required name), identical content.
Task-5 numbers re-measured (machine was noisy — interleaved ORIG/OPT runs,
medians: bzip2 10.5%/10.5% wall/user of 15 runs; sgcc_base 11.6%/11.2% of 5 runs,
prof_time 9999 both). `ex4.zip` rebuilt (bprofile.so + 5 src files, REDAME.txt).
Kept unchanged (deliberately, still better than the friend's): backward liveness
fixpoint + mem-add, SIGUSR2 disable, jrcxz skip, TCVERIFY, .init/.fini skip.
**cc1 added to the course's ex3 files on 3 Jul** (from `~/Downloads/cc1.gz`; 3.6MB
dynamic non-PIE, 2 dormant AVX-512 sites, runs natively here, ~40s): opt 6/6 +
orig 3/3 byte-identical, TCVERIFY 0, 193,417 stubs / 115,245 mem-add. Its profile
is ~33K BBLs (past the PDF's <10K hint — handled). cc1 opt-vs-orig profiles DIFFER
slightly — NOT a bug: same-tool-twice also differs (693/33,001 lines, ~0.02% count
drift; gcc hash-table/ASLR nondeterminism). Count-identity claims stay bzip2-only.
Ex3's handout confirmed: ex3 emits NO profile (Task-4's "exercise 3" reference is
a misnumbering; ex2 JIT profiler is the right baseline). Course writes "REDAME.txt"
in ex3's handout too — both README names shipped deliberately.
Real-cpugcc validation (`cpugcc_equiv2.sh`): the ex3 `cpugcc_r_base.Oz-m64_` is a
byte-identical backup (no de-AVX512 variant exists; the no-AVX512 "files from ex3"
ARE sgcc_base/sgcc_peak). But the real PIE cpugcc now translates fully under the
tool (465,103 stubs, TCVERIFY 0 — impossible before the PIE fix) and behaves
identically to native: SIGILL exit 132 both ways, no output either way. Full
functional verification on an AVX-512 CPU remains the only untested step.
