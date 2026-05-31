Exercise 2 - Edge Profiling Pintool (JIT mode)

Authors:
  Noam Grosman   ID: 318677341
  Or Ederi       ID: 314814849

Files in this submission (src/):
  ex2.cpp          - Pintool source
  makefile         - Standard Pin tool makefile
  makefile.rules   - Pin tool build rules (TOOL_ROOTS := ex2)
  README.txt       - This file

The built tool ex2.so is located at the top level of the submission zip.

Build instructions:
  Place the src/ directory contents inside a Pin tool source folder, e.g.:
      <PIN_ROOT>/source/tools/MyTools/

  Build from inside that folder with:
      make PIN_ROOT=<PIN_ROOT> obj-intel64/ex2.so

  The resulting tool binary is obj-intel64/ex2.so.

Run instructions:
  After gunzipping the supplied bzip2.gz and input.txt.gz, run:
      <PIN_ROOT>/pin -t obj-intel64/ex2.so -- ./bzip2 -k -f input.txt

  This produces two output files in the current working directory:
      edge-profile.csv   - Part 1: per-BBL edge profile,
                           sorted hottest -> coldest, with conditional
                           taken/fallthru counts and up to 10 indirect
                           target addresses sorted hottest -> coldest.
      rtn-output.csv     - Part 2: per-routine register sampling for
                           RAX, RBX, RCX, RDX, RSI, RDI (up to 20
                           successive values per register) with an
                           average delta when at least two samples exist.

Tested with Pin 4.0 (kit pin-external-4.0-99633-g5ca9893f2-gcc-linux)
on Linux x86-64. Runtime on the supplied bzip2 input: ~0.75 s
(well within the 5 s budget).
