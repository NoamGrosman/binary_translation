========================================================================
Exercise 4 - Optimizing the probe-mode profiling Pintool (bprofile)
========================================================================

(a) Authors
-----------
  Noam Grosman   ID: 318677341
  Or Ederi       ID: 314814849

(b) How to run the tool
-----------------------
Build (from inside src/):
    make PIN_ROOT=<PIN_ROOT> obj-intel64/bprofile.so

Run on the exercise-3 binaries (from the directory holding the binaries
and their inputs):

  1. bzip2 on the 20 MB text input:
       <PIN_ROOT>/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./bzip2 -k -f input-long.txt

  2. SPEC gcc on 200.i:
       <PIN_ROOT>/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./sgcc_base.mytest-m64 200.i -o 200.s
       <PIN_ROOT>/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./sgcc_peak.mytest-m64 200.i -o 200.s
       <PIN_ROOT>/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./cc1 200.i -o 200.s
       <PIN_ROOT>/pin -t obj-intel64/bprofile.so -prof_time <N> -- ./cpugcc_r_base.Oz-m64 200.i -o 200.s

     NOTE on cpugcc_r_base.Oz-m64: that binary contains unconditional
     AVX-512 instructions (e.g. "kmovd k1,eax" inside
     ggc_min_heapsize_heuristic, at offset 0x203cfb) and this machine's
     CPU (Intel Core Ultra 7 155H / Meteor Lake) has no AVX-512, so
     cpugcc dies with SIGILL even when run NATIVELY, without Pin
     (verified; the fault is in the binary/CPU combination, not in the
     Pintool). The gcc measurements below were therefore done with
     sgcc_base.mytest-m64 / sgcc_peak.mytest-m64 - the same SPEC gcc
     compiled without AVX-512 - which run natively on this machine and
     are even harder for the tool (statically-linked ~30 MB binaries
     whose glibc is translated too). The PIE-specific translation paths
     that cpugcc needs (it is a position-independent executable loaded
     at a high address) were implemented (fix 6 below) and verified two
     ways: (1) a synthetic PIE test program containing an indirect jump
     through a RIP-based memory operand runs under the tool with
     identical output, a correct indirect-target profile and a clean
     verifier; (2) the tool was run on the real cpugcc as far as this
     machine allows - the whole PIE image translates successfully
     (465,103 profiling stubs emitted, TC verifier reports 0 bad
     branch/rip targets) and the translated program then behaves
     exactly like the native run: both die on the same unsupported
     AVX-512 instruction with SIGILL (exit code 132 in both cases)
     after executing the translated startup path, producing no output
     either way.

     UPDATE (2026-07-04): full functional verification of cpugcc was
     completed on an AVX-512-capable machine (AMD Ryzen 7 7800X3D,
     Zen 4). Under the tool (probe mode, -prof_time 10) cpugcc runs
     200.i to correct completion: 3/3 runs of the optimized tool and
     1/1 run of the unoptimized baseline produced a 200.s byte-identical
     to the native run on that machine (3,873,835 bytes), each with a
     clean TC verifier ("TCVERIFY: 0 bad branch/rip targets"). The same
     session also re-verified sgcc_peak (2/2), cc1 (2/2), sgcc_base
     (2/2) and bzip2 (byte-identical outputs, TCVERIFY 0) on that CPU.
     Nothing in this submission is untested anymore.

The profile is written to edge-profile.csv in the working directory on
program exit, one line per executed BBL, hottest first:

    <bbl addr>, <exec count>, <taken count>, <fallthru count>[, <indirect
    target addr>, <its count> ... up to 4 targets, sorted by count]

taken/fallthru are derived from the BBL terminator: for a conditional
branch, fallthru is the collected fall-through counter and taken =
exec - fallthru; for an unconditional jump (direct or indirect) or ret,
taken = exec; for a BBL that ends at a call or because the next
instruction is a jump target, fallthru = exec. -prof_time <N> controls
how many seconds of the run are profiled before the profiling stubs are
disabled (default 2).

(c) Problems fixed in the provided pintool, and the Task-2 optimization
-----------------------------------------------------------------------
Task 1 fixes (needed to run the binaries to correct completion):

  1. Conditional branches to untranslated code aborted the translation.
     fix_direct_jmp_or_call_to_orig_addr() only handles CALL and
     unconditional JMP (it reroutes them through a memory-indirect
     jmp/call via jump_to_orig_addr_map). When a Jcc targeted an address
     that was not translated (a routine Pin skipped), translation failed
     with "Invalid direct jump ... jnz" and the tool silently fell back to
     running the ORIGINAL untranslated code - i.e. no profiling at all.
     Fix (in fix_direct_jmp_or_call_displacement): a Jcc has no
     memory-indirect form, so the rel32 displacement is pointed back at
     the original (untranslated) target; the existing 32-bit-displacement
     check still validates it.

  2. Probe-replacing the ELF startup/teardown stubs corrupted the
     statically-linked gcc (SIGSEGV during startup). The section filter
     now skips .init/.fini (and .plt/.plt.got/.plt.sec), matching how
     these non-function code stubs must be left to run natively.

  3. Silently-truncated 1-byte branch displacements.
     fix_direct_jmp_or_call_displacement() forces LOOP/LOOPcc/JRCXZ to a
     1-byte displacement. The profiling stubs inflate the distance
     between such a branch and its target; once it exceeds +-127, the
     re-encode silently wraps the displacement and the branch lands 256
     or 512 bytes past its target, in the middle of other instructions.
     In sgcc exactly two instructions are affected ("jrcxz" in glibc's
     __mpn_add_n and __mpn_sub_n), which caused wild, layout-dependent
     crashes. We added a post-fixup verifier that decodes every branch in
     the final TC and checks it lands on its intended target (it found
     exactly these two), and we now leave routines containing
     LOOP/LOOPcc/JRCXZ untranslated (verifier reports 0 bad targets
     afterwards).

  4. The tool's big RWX region (TC + maps) was mmapped directly after the
     image, i.e. exactly where a non-PIE static binary's brk heap lives.
     Whenever the kernel's randomized brk base landed under that region,
     the program's very FIRST sbrk - the pre-malloc TLS allocation in
     __libc_setup_tls - failed, and since that code path never checks for
     sbrk failure, the TLS block address computed from (-1 + align-1) &
     -align == 0 and the startup memcpy wrote to address 0x830 => an
     intermittent, ASLR-dependent crash (this exact 0x830 signature).
     Fix: place the region ~1GB above the image - far beyond any brk
     growth, still within the 32-bit branch displacement the translated
     code needs. (Dynamically-linked bzip2 was immune: its heap is not
     placed after the main image.)

  5. disable_profiling_in_tc() patches a 5-byte jmp over each stub head
     with memcpy WHILE the application is executing that code. The
     first (opcode) byte is written before the displacement bytes, so a
     concurrently-fetching thread can decode a jmp with a half-written
     displacement and fly off into unused TC memory (we observed exactly
     that: intermittent post-"disabling" crashes with rip in no-man's
     land / SIGTRAPs). PIN_StopApplicationThreads turned out not to work
     in probe mode (it caused SIGTRAP kills), so the fix splits the work:
     the timer thread only PREPARES the patch list (all XED encoding),
     then sends SIGUSR2 to the application (SIGUSR2 blocked on the timer
     thread), and the patches are applied inside the signal handler -
     i.e. ON the single application thread, which by construction cannot
     be executing a stub while it is inside the handler. A 5s timeout
     falls back to direct patching if the signal is never serviced.

  6. PIE binaries broke the indirect-jump profiling stub. When
     add_profiling_instrs() converts "jmp [rip+disp]" into
     "mov rax, [absolute addr]", the absolute address of a PIE (loaded
     at 0x55...) does not fit in a 32-bit displacement, and the
     translation of the whole image was aborted. Fix: when the address
     does not fit, load it into a scratch register with a 64-bit
     immediate MOV and use that register as the base of the load
     (scratch = RBX, or RCX when RBX is the jump's index register; both
     are clobbered by the indirect-target code right after, and their
     original values were already saved when live). Verified with a
     synthetic PIE program: output identical, indirect targets profiled
     correctly, TC verifier clean.

  7. Error paths of find_candidate_rtns_for_tc() returned while the
     current RTN was still open, so any translation failure made Pin die
     on the "Must use RTN_Close on previous rtn before opening a new
     rtn" assert (exit 127) instead of gracefully falling back to
     running the original code. RTN_Close() was added to every error
     path.

  8. The indirect-jump stub clobbered RFLAGS while they can be LIVE.
     The provided stub masks the jump target with "AND RAX, 0x3" to
     index its 4-entry target table - and AND writes the flags. The -O3
     code of sgcc_peak keeps a comparison result in the flags ACROSS an
     indirect jump and conditionally branches on it at the jump target;
     with the stub inserted, sgcc_peak ran to completion but silently
     produced WRONG OUTPUT (reproduced on this machine: 200.s came out
     3,873,859 bytes instead of the native 3,873,835). Fix: compute the
     masking without touching RFLAGS using the BMI2 non-flag-writing
     shifts:  MOV RCX, 62 ; SHLX RAX, RAX, RCX ; SHRX RAX, RAX, RCX
     (RCX is free at that point: it was already saved or proven dead,
     and the following stub instructions overwrite it anyway). After the
     fix sgcc_peak's output is byte-identical to native in 6/6 runs.
     The same reasoning also changed the liveness boundary condition and
     the counter-increment choice at indirect jumps - see Task 2 below.

  9. Routines whose last instruction can fall through past the routine
     end (a plain instruction, a call, or the fall-through side of a
     conditional branch). In the original code execution continues at
     the next address - which may be code Pin attributes to no routine
     (symbol-table gaps in the static glibc of sgcc_base/sgcc_peak) and
     is therefore never translated. In the TC, the next bytes belong to
     the translation of an unrelated routine, so falling through would
     execute wrong code with live registers. Fix: after translating such
     a routine, append an explicit "jmp <fall-through address>" (plus
     the closing BBL-counter stub); the chaining step redirects it to
     the translated copy of that address when one exists, and otherwise
     it becomes an indirect jump back to the original code.

 10. Probe-safety guards in commit_translated_rtns_to_tc(): skip
     routines for which RTN_IsSafeForProbedReplacement() returns false
     (otherwise Pin aborts the whole run) and routines smaller than
     8 bytes (RTN_ReplaceProbed fails on them and on some Pin versions
     corrupts the probe trampolines of neighboring routines). Such
     routines simply keep running their original code.

Task 2 - skipping save/restore of dead registers:

  Where the liveness comes from: while each routine is open we collect,
  per instruction, which of RAX/RBX/RCX are read, and which are killed
  (fully overwritten - only 64-bit and 32-bit zero-extending writes kill;
  writes by predicated instructions such as CMOVcc/REP never kill). The
  six status flags are tracked as one unit using XED's per-instruction
  rflags info (read set; killed only when all six are unconditionally
  written or undefined). A standard backward dataflow fixpoint then
  yields live-in for every instruction. Boundary conditions are ABI-based
  and conservative:
    * ret            : RAX (return value) and RBX (callee-saved) live;
                       RCX and flags dead across a return.
    * call           : callee may read RAX (varargs count) and RCX (4th
                       arg); RAX/RCX/flags come back clobbered; RBX flows
                       through (callee-saved).
    * indirect jmp   : RAX/RBX/RCX live (unknown target), and the status
                       flags LIVE as well - fix 8 above showed that -O3
                       code really does carry flag values across indirect
                       jumps, so the old assumption (flags dead because
                       the stub's own AND clobbered them anyway) was
                       unsound and the stub now preserves the flags.
    * any target we cannot see (branch out of the routine, fall out of
      the routine's last instruction): everything live.

  What the stub emission does with it (add_profiling_instrs receives a
  dead-register mask for the exact stub location - the BBL-count stub
  uses live-in of the terminating instruction; the fall-through stub
  after a Jcc uses live-in of the next instruction):
    * flags dead  => the whole 5-instruction RAX-based increment
                     (save RAX; MOV RAX,[c]; LEA RAX,[RAX+1]; MOV [c],RAX;
                     restore RAX) collapses to a single RIP-relative
                        ADD qword ptr [counter], 1
                     with no register use at all. To make the counter
                     RIP-reachable, bbl_map was moved out of the heap into
                     the same 32-bit-reachable mmap region as the TC.
                     (The ADD writes the flags, so it is used ONLY when
                     they are provably dead - in particular it is no
                     longer used unconditionally in indirect-jump stubs;
                     when the flags are live there, the flag-transparent
                     MOV/LEA/MOV sequence is emitted instead.)
    * RAX dead    => the MOV [rax_mem],RAX save and MOV RAX,[rax_mem]
                     restore are dropped (the LEA path just clobbers RAX).
    * RBX/RCX dead=> their two-instruction save and restore pairs in the
                     indirect-jump stub are dropped.
  Every stub still begins with the same >=5-byte wide NOP, so
  disable_profiling_in_tc() keeps overwriting stub heads with the 5-byte
  skip jmp exactly as before.

  Correctness validation: with a -prof_time larger than the entire run
  (so profiling is never disabled), the optimized tool's
  edge-profile.csv is byte-identical to the unoptimized tool's on the
  same run for bzip2 (870 lines compared with cmp; bzip2 is the
  deterministic workload - the gcc binaries are internally
  nondeterministic run-to-run because of pointer-keyed hash tables and
  ASLR, so their counts drift ~0.02% between ANY two runs, even of the
  same tool, and exact count identity is not expected there). The
  programs' outputs stay byte-identical to native runs: bzip2 3/3 runs,
  sgcc_base 6/6 runs per tool, sgcc_peak 6/6 runs, cc1 6/6 runs
  (optimized) + 3/3 runs (unoptimized), each compared with cmp; the
  built-in TC verifier reports 0 bad targets on every run. (cc1's
  profile has ~33,000 executed BBLs - well past the exercise's
  "assume <10,000" sizing hint - and is handled fine; the BBL table is
  sized from the decoded image, not a fixed constant.)

(d) Differences vs. the exercise-2 (JIT) edge profiler
------------------------------------------------------
Reference: the exercise-2 JIT-mode edge profiler (ex2.so) - the only
prior tool that emits edge-profile.csv (exercise 3 is a translator with
no profile output). Since the committed ex2 profile was produced on the
short input, we re-ran ex2.so on the SAME binary and input used here
(bzip2 -k -f input-long.txt) and diffed it against this tool's
edge-profile.csv from a run whose -prof_time exceeds the whole run
(so both count the full execution).

Headline: of this tool's 870 emitted BBLs, 685 carry the exact same
execution count as ex2's line for the same BBL address, including the
entire hot loop of bzip2 (e.g. "0x40a7c3, 148638693, 137754781,
10883912" appears verbatim in both), and 254 lines - the
conditional-branch lines, where both tools emit all four fields - are
byte-for-byte identical. (The remaining field-level difference is
format-only: per the exercise-4 PDF this tool emits taken/fallthru on
EVERY line, while ex2 emits them only for conditional branches.)
Counts agree exactly wherever the two tools agree on BBL boundaries.
The large differences and their causes:

  1. Coverage: probe mode translates only the main executable's
     routines; ex2 (JIT) instruments every image. ex2's profile contains
     4787 lines vs our 870; e.g. a 12.6M-count BBL at 0x7b9125188d87 is
     inside libc.so, which this tool never sees. A few main-image
     routines are also missing here because RTN_ReplaceProbed rejects
     them or because we skip them (.plt/.init/.fini, LOOP/JRCXZ
     routines, unsafe-for-probe or <8-byte routines), whereas JIT mode
     covers them all.

  2. BBL segmentation: this tool splits statically - a BBL ends at a
     jump/ret or before a target of some DIRECT branch of the same
     routine. Pin's JIT discovers BBLs dynamically: BBLs also end at
     CALLs, and new BBL heads appear at run-time-discovered targets of
     indirect jumps and at return sites. Hence ex2 has heads we never
     create (e.g. 0x40ad2e/0x40ae1c/0x40af0a, ~17M each - dispatch
     targets inside mainSimpleSort), and we emit heads ex2 folds into a
     predecessor (e.g. our hottest line 0x40a78b, a fall-through
     continuation that JIT keeps inside a larger trace-shaped BBL).

  3. Indirect-target profiling: this tool records targets only for
     BBLs terminating in a true indirect JUMP (not RET and not CALL),
     into 4 buckets hashed by the target's two low address bits, so at
     most 4 approximate targets survive and two targets can collide in
     a bucket. ex2 records precise per-target counts (up to 10) and
     also treats RETURNS as indirect flow. That is why ex2's
     "0x40abe6, 50841127, 0x40ad2e, 17058402, ..." (mainGtU's shared
     "pop rbp; ret" epilogue - identical 50,841,127 execution count in
     both tools!) carries return-target pairs in ex2 but reads
     "0x40abe6, 50841127, 50841127, 0" here (a ret is always "taken").

  4. Sampling window: with the intended usage (-prof_time N smaller
     than the run), this tool counts only the first N seconds, so all
     counts are a prefix-window sample of ex2's full-run counts. The
     comparison above deliberately used a window larger than the run to
     factor this effect out; with, say, -prof_time 2 on bzip2 every
     count shrinks by roughly the fraction of the run not profiled.

(e) Performance improvement (Task 5)
------------------------------------
Setup: bprofile_orig.so is the tool with all of the Task-1 correctness
fixes ((c) 1-10, including the flag-free SHLX masking) and the Task-3
CSV output, but with the ORIGINAL, unoptimized stub emission;
bprofile.so adds the Task-2 dead-register optimization (both are built
by the provided makefile). Measured with /usr/bin/time -v; ORIG and OPT
runs were INTERLEAVED (orig,opt,orig,opt,...) so background-load drift
affects both tools equally, and medians are compared. Both tools were
given the same -prof_time. -prof_time 9999 (longer than the whole run)
is used so profiling is never disabled and the entire run pays the stub
cost - the direct measure of the stub optimization. (perf is not
installed on this machine, so the cycles metric was unavailable;
elapsed/user/system time and page faults were collected.)

1) bzip2 -k -f input-long.txt, -prof_time 9999 (medians of 15 runs per
   tool - three interleaved 5-run sessions):

   metric            orig (median)   optimized (median)   improvement
   elapsed (wall)    9.75 s          8.73 s               10.5%   *
   user time         9.71 s          8.69 s               10.5%   *
   minor faults      77,610          77,400                0.3%

2) sgcc_base.mytest-m64 200.i -o 200.s, -prof_time 9999 (medians of 5
   interleaved runs per tool; the optimized tool was faster in all 5
   back-to-back orig/opt pairs):

   metric            orig (median)   optimized (median)   improvement
   elapsed (wall)    61.21 s         54.11 s              11.6%   *
   user time         64.55 s         57.29 s              11.2%   *
   minor faults      925,199         890,021               3.8%

   raw rows for both experiments are in measure_bzip.txt /
   measure_sgcc_full.txt.

(*) >= 5% improvement. The requirement is met on BOTH binaries, on both
the elapsed-time and the user-time metrics.

Why it wins: on bzip2, 2822 of the 4140 profiling stubs (68%) collapse
from 6 instructions / 4 memory accesses to a NOP + a single
ADD qword ptr [counter],1, and another 1013 drop the 2 RAX save/restore
memory accesses; the same ratios hold for sgcc_base (455,069 of 753,346
stubs use the single ADD, 30,420 more skip the RAX save).

Debug aids kept in the tool (all default-off): -no_mem_add /
-no_skip_dead disable each sub-optimization, -dump_around 0xADDR dumps
the translated code around a TC or original address, and a built-in
post-fixup verifier decodes the final TC and reports any branch or
RIP-relative operand that does not land on its intended target
("TCVERIFY: 0 bad branch/rip targets" on a good translation).
