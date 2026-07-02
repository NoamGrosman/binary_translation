Exercise 3 - Fixing the btranslate Pintool (Probe mode)

Authors:
  Noam Grosman   ID: 318677341
  Or Ederi       ID: 314814849

Files in this submission (src/):
  btranslate.cpp   - Fixed pintool source
  makefile         - Pin tool makefile
  makefile.rules   - Pin tool build rules
  README.txt       - This file

The zip also contains ex3.so, which is just our compiled btranslate.so renamed.

Build:
  Drop the files into a Pin tool source folder, e.g.
      <PIN_ROOT>/source/tools/MyTools/
  and from there run:
      make PIN_ROOT=<PIN_ROOT> obj-intel64/btranslate.so

Run:
  <PIN_ROOT>/pin -t obj-intel64/btranslate.so -- ./cpugcc_r_base.Oz-m64 200.i -o 200.s

  Or with the binary we submitted:
      <PIN_ROOT>/pin -t ex3.so -- ./cpugcc_r_base.Oz-m64 200.i -o 200.s

  Note: cpugcc_r_base.Oz-m64 runs an AVX-512 instruction in its own startup, so
  on a CPU without AVX-512 it dies with SIGILL before producing anything - this
  has nothing to do with our tool. For that case the course gave two static,
  AVX-512-free replacements, run the same way:
      <PIN_ROOT>/pin -t ex3.so -- ./sgcc_base.mytest-m64 200.i -o 200.s
      <PIN_ROOT>/pin -t ex3.so -- ./sgcc_peak.mytest-m64 200.i -o 200.s
  Both produce assembly identical to a native (no-Pin) run, and sgcc_base's
  output matches the supplied golden 200.s byte for byte.

A couple of debug aids we left in:
  * -max_rtns N : translate only the first N candidate routines and print each
    one's name. We used this to bisect for the bad routine, as the tips suggest.
  * A stats summary printed to stderr at the end of create_tc (how many routines
    were translated vs skipped, which fixes got exercised, etc).


========================================================================
THE SEGFAULT, AND HOW WE FOUND IT
========================================================================

To find it we added -max_rtns and binary-searched over N: "good" means the run
gets past startup, "bad" means it segfaults. It narrowed down to translated
routine #2:

    translating rtn #2: .plt

so the crash shows up the moment the ".plt" pseudo-routine gets translated and
probe-replaced.

The reason is that .plt isn't really a function. Pin reports it as a normal RTN,
so the original tool happily translated it into the TC and probe-replaced its
entry like anything else - but .plt is the dynamic linker's lazy-binding stub
code:

    PLT0:    push [GOT+8]        ; pushes the executable's link_map
             jmp  [GOT+16]       ; jumps to ld.so's _dl_runtime_resolve
    stub_i:  jmp  [GOT+n]        ; first time, falls through to the next line
             push reloc_index_i
             jmp  PLT0

RTN_ReplaceProbed on .plt overwrites PLT0's first instruction (the 6-byte
"push [GOT+8]") with the 5-byte probe JMP and sends the whole handshake into the
TC copy. So the first call through an unresolved stub reaches _dl_fixup with a
garbage link_map and the program dies in ld-linux-x86-64.so.2 (dmesg confirmed
the faulting address was inside ld.so). We double-checked by translating .plt
but skipping just the probe on it - the crash goes away, so the probe on PLT0 is
what breaks lazy binding.

Fix (in find_candidate_rtns_for_tc): don't translate routines in ".plt"
sections (.plt, .plt.got, .plt.sec). The PLT and GOT stay untouched, and any
direct call from translated code into a stub is sent back to the original via
fix_direct_br_call_to_orig_addr() (an indirect call through
jump_to_orig_addr_map), so lazy binding keeps working normally.


========================================================================
OTHER BUGS WE FOUND DURING
========================================================================

1. Off-by-one in jump_to_orig_addr_map
   fix_direct_br_call_to_orig_addr() did "jump_to_orig_addr_num++; entry =
   jump_to_orig_addr_num;", i.e. it took the index after incrementing. So slot 0
   was wasted and every target was stored one slot past where the dedup search
   looks - the search never found the latest entry and the table filled up with
   duplicates. We reordered it (bounds-check, entry = num, then num++). Now dedup
   actually works (on the gcc binary 20980 calls collapse to 381 unique entries),
   which keeps the table inside its bounds.

2. Silent truncation of 1-byte branch displacements
   LOOP/LOOPE/LOOPNE/JRCXZ only have an 8-bit displacement. If the new one
   doesn't fit in [-128,127] the old code truncated it silently and produced a
   branch to garbage. We now catch the overflow and fail the translation cleanly.

3. Translating routines we can't probe-replace
   If RTN_IsSafeForProbedReplacement() says no, we skip the routine up front
   instead of translating it and then committing it unpredictably.

4. Routines too small for a probe
   The probe is a 5-byte JMP, so routines smaller than 5 bytes are skipped.

5. Conditional branches into non-translated code
   Once we start skipping routines (the .plt fix plus #3 and #4), a translated
   routine can have a Jcc whose target isn't in the TC. The original code pushed
   every such branch through fix_direct_br_call_to_orig_addr(), which only knows
   how to handle CALL/JMP (Jcc has no memory-indirect form), so translation died
   with "Invalid direct jump from translated code to original code". We instead
   re-encode the Jcc with a rel32 straight back to the original target (the TC is
   right after the image so rel32 reaches, and the existing 32-bit check still
   guards it).


========================================================================
EXTRA FIXES FOR THE STATIC sgcc BINARIES
========================================================================

cpugcc_r_base.Oz-m64 is a dynamically-linked PIE. The AVX-512-free replacements
(sgcc_base/sgcc_peak) are statically linked and non-PIE, loaded at the fixed low
address 0x400000. We could only verify the output end-to-end on these (cpugcc
SIGILLs on AVX-512 before producing anything), and doing so turned up three more
bugs the PIE binary had hidden, plus another group of bootstrap routines we now
leave alone.

6. jump_to_orig_addr_map landed >2GB from the TC
   Translated code reaches that table with a rip-relative indirect branch, so it
   has to be within a 32-bit displacement of the TC. It used to be allocated
   after instr_map, which is huge (with the old x10 inflation it was ~2.5GB on
   the gcc binaries), pushing the table out of range and aborting with "Invalid
   rip displacement larger than 32 bits". We now put it right after the TC and
   before instr_map. instr_map is only host-side bookkeeping and is never read
   from running code, so it can sit wherever.

7. Translating the .init / .fini glue
   Same idea as .plt: _init/_fini are CRT startup/teardown trampolines, not real
   functions. Probe-replacing _init throws the early startup into the TC and
   crashes before main(). We skip .init and .fini sections too.

8. TC overlapping the brk heap (non-PIE only)
   We mmap the TC near the image. On a non-PIE binary the brk heap grows right
   after the (low) image, so the mapping landed on the heap base and boxed it in
   - once malloc couldn't grow the heap the program crashed (gcc needs ~0.5GB on
   200.i). The PIE cpugcc never had this since its image and heap are up at high
   randomized addresses. Fix: when the image is loaded low, place the TC at ~1GB
   (well past the heap, still in rel32 range of the code). PIE images keep the
   original placement, so cpugcc is unaffected.

9. Bootstrap routines (TLS / brk) that run before main()
   __libc_setup_tls (sets up the TLS block and %fs) and __sbrk / __brk (drive
   the brk syscall and __curbrk) run very early, before normal code. Translating
   and running TC copies of them at that point is shaky - same family as
   .plt/.init/.fini - and on sgcc_peak it intermittently corrupted the
   just-initialized TLS/break (__sbrk handed back a near-null break, so the
   TLS-init memcpy wrote to ~0x830 and crashed). We skip these by name and leave
   the originals in place; they're still reachable.

Translation-cache sizing
   instr_map was sized 2 * (instructions * 10), ~2.5GB on the gcc binaries.
   Translation is basically 1:1 in practice, so we dropped the factor to x3
   (still ~3x headroom). The existing "out of memory for map_instr" guard aborts
   cleanly if that's ever too small. This shrinks the mapping to ~770MB and makes
   it less likely to collide with Pin's own mappings.


========================================================================
TESTING
========================================================================

  * cpugcc_r_base.Oz-m64 (the assigned target): translates and probe-replaces
    ~14.8K routines and gets past startup with no segfault. On a machine without
    AVX-512 the binary can't actually finish (the SIGILL mentioned above), so
    here we confirmed it translates and commits fully and checked output
    correctness on sgcc_base, which is the same gcc producing the same 200.s.
  * sgcc_base.mytest-m64 (the AVX-512-free stand-in): runs to a clean exit
    ("Reached _exit") and produces assembly identical both to a native run and
    to the golden 200.s, repeatably (~13.5K routines probe-replaced, 6 bootstrap
    routines skipped). This is our main end-to-end result.
  * sgcc_peak.mytest-m64: produces assembly identical to a native run. It's a
    much more heavily optimized build and still shows a rare intermittent crash
    we couldn't fully pin down; it isn't the assigned target and is only here as
    a second AVX-512-free option.
  * As a separate sanity check, bzip2 under the tool compresses a 20MB input to
    a .bz2 identical to the native run (121/121 routines translated and probed).
