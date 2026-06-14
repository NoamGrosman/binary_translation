Exercise 3 - Fixing the btranslate Pintool (Probe mode)

Authors:
  Noam Grosman   ID: 318677341
  Or Ederi       ID: 314814849

Files in this submission (src/):
  btranslate.cpp   - Fixed pintool source
  makefile         - Pin tool makefile
  makefile.rules   - Pin tool build rules
  README.txt       - This file

The zip also contains ex3.so (the compiled pintool, renamed from btranslate.so).

Build instructions:
  Place the files inside a Pin tool source folder, e.g.:
      <PIN_ROOT>/source/tools/MyTools/

  Build from inside that folder with:
      make PIN_ROOT=<PIN_ROOT> obj-intel64/btranslate.so

How to run:
  <PIN_ROOT>/pin -t obj-intel64/btranslate.so -- ./cpugcc_r_base.Oz-m64 200.i -o 200.s

  (or with the submitted binary directly:
      <PIN_ROOT>/pin -t ex3.so -- ./cpugcc_r_base.Oz-m64 200.i -o 200.s)

Debug aids we added:
  * Knob -max_rtns N : translate only the first N candidate routines and log
    each translated routine name. This is how we isolated the faulty routine,
    by binary search over N (as suggested in the exercise tips).
  * A statistics summary printed to stderr at the end of create_tc, showing
    how many routines were translated/skipped and which fixes were exercised.

========================================================================
THE BUG THAT CAUSED THE SEGFAULT, AND HOW WE FOUND IT
========================================================================

How we isolated it:
  We added the -max_rtns knob to cap the number of translated routines and
  ran an automated binary search over N ("good" = the run gets past startup,
  "bad" = segfault). The search converged on translated routine #2:

      translating rtn #2: .plt

  i.e. the segfault appears as soon as the ".plt" pseudo-routine is
  translated and probe-replaced.

Root cause:
  Pin exposes the .plt section of the executable as a regular RTN, so the
  original tool translated it into the translation cache (TC) and
  probe-replaced its entry point like any other routine. But .plt is not a
  normal function - it is the dynamic linker's lazy-binding machinery:

    PLT0:    push [GOT+8]        ; pushes the link_map of the executable
             jmp  [GOT+16]       ; jumps to ld.so's _dl_runtime_resolve
    stub_i:  jmp  [GOT+n]        ; initially points back to the next line
             push reloc_index_i
             jmp  PLT0

  RTN_ReplaceProbed() on the .plt RTN overwrites PLT0's first instruction
  (the 6-byte "push [GOT+8]") with the 5-byte probe JMP and redirects the
  whole lazy-binding handshake into the TC copy. As a result, the first call
  through any not-yet-resolved PLT stub enters ld.so's _dl_fixup() with a
  corrupted link_map argument, and the program dies with SIGSEGV inside
  ld-linux-x86-64.so.2 (confirmed in dmesg: faulting IP inside ld.so,
  dereferencing a garbage pointer while reading the link_map's l_info).

  We confirmed the mechanism by an experiment: translating .plt into the TC
  but skipping only the probe placement on it makes the segfault disappear -
  so the probe on PLT0 is exactly what breaks lazy binding.

The fix (in find_candidate_rtns_for_tc):
  Never translate routines that live in ".plt" sections (.plt, .plt.got,
  .plt.sec). The original PLT and GOT then stay fully intact, and direct
  calls from translated code to PLT stubs are routed back to the original
  stubs through fix_direct_br_call_to_orig_addr() (an indirect call via the
  jump_to_orig_addr_map), so lazy binding keeps working exactly as the
  dynamic linker expects.

========================================================================
ADDITIONAL BUGS WE FOUND AND FIXED ALONG THE WAY
========================================================================

Bug A - off-by-one in jump_to_orig_addr_map allocation
  fix_direct_br_call_to_orig_addr() allocated map entries with
  "jump_to_orig_addr_num++;  entry = jump_to_orig_addr_num;" - i.e. the
  entry index was taken AFTER the increment. Slot 0 was never used, each
  new target was stored one slot past where the dedup search looks, so the
  search never found the most recent entry and the table filled with
  duplicates. Fixed the order (bounds-check, then entry = num, then num++).
  With the fix the dedup works (e.g. 20980 calls collapse into 381 unique
  entries on the gcc binary), keeping the table within its bounds.

Bug B - silent truncation of 1-byte branch displacements
  LOOP/LOOPE/LOOPNE/JRCXZ only have an 8-bit displacement. If the new
  displacement after translation does not fit in [-128,127] it used to be
  silently truncated, producing a branch to a garbage address. We now
  detect the overflow and fail the translation cleanly instead.

Bug C - translating routines that cannot be probe-replaced
  Routines for which RTN_IsSafeForProbedReplacement() returns false are now
  skipped up front instead of being translated and then (unpredictably)
  committed.

Bug D - translating routines too small for a probe
  A probe is a 5-byte JMP; routines with RTN_Size() < 5 are now skipped.

Bug E - conditional branches to non-translated code
  Once routines are skipped (the .plt fix and bugs C/D), a translated
  routine may contain a Jcc whose target is not in the TC. The original
  code routed every such branch through fix_direct_br_call_to_orig_addr(),
  which only supports CALL/JMP (Jcc has no memory-indirect form), so the
  whole translation failed with "Invalid direct jump from translated code
  to original code". We now re-encode such a Jcc with a rel32 displacement
  straight back to the original target address (the TC is allocated right
  after the image, so rel32 always reaches; the existing 32-bit
  displacement check still guards this).

========================================================================
TESTING
========================================================================

  * cpugcc_r_base.Oz-m64: the tool translates and probe-replaces ~14.8K
    routines and the program proceeds past startup (no segfault), producing
    the 200.s output.
  * Independent end-to-end check: running bzip2 under the fixed tool
    compresses a 20MB input to a byte-identical .bz2 compared to the native
    run (121/121 routines translated and probed).
