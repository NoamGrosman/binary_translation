========================================================================
src_fixed/ - EXPERIMENTAL fixed pintool (handles large static binaries too)
========================================================================

WHAT THIS IS
  This directory is a SEPARATE, edited copy of the Exercise-3 pintool. It is
  NOT the submission. The submitted/working solution is left exactly as-is:

      ../src/btranslate.cpp     ../ex3.so     ../ex3.zip     (UNCHANGED)

  This copy adds two small fixes so the tool also correctly translates the
  large, statically-linked SPEC-gcc binaries that contain AVX-512 code:

      ../sgcc_base.mytest-m64
      ../sgcc_peak.mytest-m64

  The exercise itself only requires cpugcc_r_base.Oz-m64; that case still
  passes byte-for-byte here (no regression). These extra fixes are kept
  separate so both the old and new tools are available for side-by-side
  review and testing.

  See fix.diff in this directory for the exact change set (old src -> here).

------------------------------------------------------------------------
THE TWO FIXES (both static-binary specific; neither is AVX-512 encoding)
------------------------------------------------------------------------

It turned out the AVX-512 instructions themselves translate fine. The static
binaries failed for two unrelated reasons, both fixed below, both confined to
the functions that already do the analogous work.

FIX 1 - allocate_and_init_memory(): keep instr_map OFF the executable mmap
  Original layout of the single mmap was:  [ TC ][ instr_map ][ jump_map ].
  instr_map is sized 2 * max_ins_count * sizeof(instr_map_t); for a ~54 MB
  static binary that is multiple GB. Two consequences:
    (a) jump_to_orig_addr_map sat *after* that multi-GB array, so the
        RIP-relative (disp32) indirect jmp/call that translated code uses to
        reach it overflowed the signed 32-bit displacement -> translation
        aborted ("Invalid rip displacement larger than 32 bits ...").
    (b) the multi-GB low-memory reservation collided with the heap/brk of a
        non-PIE static target -> the program SIGSEGV'd at run time (this
        happened even with NO probing at all).
  Fix: instr_map is tool-internal bookkeeping only - it is never referenced by
  a RIP-relative displacement from translated code - so it is allocated on the
  ordinary heap with calloc(), OUT of the executable mmap. The mmap now holds
  only [ TC ][ jump_map ] (<= ~128 MB), with jump_map placed immediately after
  the TC. That fixes both (a) and (b).

FIX 2 - find_candidate_rtns_for_tc(): also skip the .init / .fini sections
  Pin exposes _init / _fini (the ELF startup/teardown stubs in .init/.fini)
  as ordinary RTNs. Probe-replacing them corrupts the program's startup -
  exactly the same class of problem as the original .plt bug. Isolated by
  bisection: probing even the first routine (_init) crashed the static binary.
  Fix: skip .init and .fini sections, mirroring the existing .plt skip; _init
  and _fini then run natively (just like .plt stubs do).

  NOTE (cosmetic): the .init/.fini skips currently increment the existing
  "skipped .plt sections" stats counter, so that line in the summary now also
  counts .init/.fini. Harmless; only the printed label is approximate. Giving
  them a dedicated counter is an optional tidy-up.

------------------------------------------------------------------------
VERIFICATION (this build, on this machine)
------------------------------------------------------------------------
  Reference: a native (no-Pin) run of each binary on 200.i produces assembly
  whose sha256 is 6f3d6534a299b56d87fe7c4a17c0fbaa60a946dfa9f2535c40565ef5e76e562d
  (identical for all three binaries, and identical to the provided ../200.s).

  Running each binary under ../ex3_fixed.so with "200.i -o out.s":
    cpugcc_r_base.Oz-m64 : PASS  (byte-identical to 200.s, clean exit)
    sgcc_base.mytest-m64 : PASS  (byte-identical to 200.s, clean exit)  [was SIGSEGV]
    sgcc_peak.mytest-m64 : PASS  (byte-identical to 200.s, clean exit)  [was SIGSEGV]

------------------------------------------------------------------------
HOW TO BUILD
------------------------------------------------------------------------
  From inside this directory:
      make PIN_ROOT=<PIN_ROOT> obj-intel64/btranslate.so
  (PIN_ROOT used here: ~/pin-external-4.0-99633-g5ca9893f2-gcc-linux)

  The prebuilt result is also provided as ../ex3_fixed.so (identical bytes to
  obj-intel64/btranslate.so).

------------------------------------------------------------------------
HOW TO TEST / COMPARE OLD vs NEW (run from the ex3/ directory)
------------------------------------------------------------------------
  PIN=<PIN_ROOT>/pin
  # the sgcc binaries are downloaded without the exec bit:
  chmod +x sgcc_base.mytest-m64 sgcc_peak.mytest-m64 cpugcc_r_base.Oz-m64

  # OLD tool (submitted) - sgcc_* will SIGSEGV, cpugcc works:
  $PIN -t ex3.so       -- ./sgcc_base.mytest-m64 200.i -o /tmp/old.s ; echo rc=$?

  # NEW tool (this fix) - all three succeed:
  $PIN -t ex3_fixed.so -- ./sgcc_base.mytest-m64 200.i -o /tmp/new.s ; echo rc=$?
  $PIN -t ex3_fixed.so -- ./sgcc_peak.mytest-m64 200.i -o /tmp/new_peak.s ; echo rc=$?
  $PIN -t ex3_fixed.so -- ./cpugcc_r_base.Oz-m64 200.i -o /tmp/new_cpugcc.s ; echo rc=$?

  # correctness = byte-identical to the reference 200.s:
  cmp /tmp/new.s 200.s && echo "sgcc_base: identical"
  cmp /tmp/new_peak.s 200.s && echo "sgcc_peak: identical"
  cmp /tmp/new_cpugcc.s 200.s && echo "cpugcc: identical"

------------------------------------------------------------------------
FILES IN THIS DIRECTORY
------------------------------------------------------------------------
  btranslate.cpp            fixed source (../src/btranslate.cpp + the 2 fixes)
  fix.diff                  unified diff: ../src/btranslate.cpp -> this file
  makefile, makefile.rules  unchanged copies of the build files
  obj-intel64/btranslate.so the built fixed tool (== ../ex3_fixed.so)
  README_FIX.txt            this file
